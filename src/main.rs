#![deny(clippy::unwrap_used, clippy::expect_used)]
#![forbid(unsafe_code)]
use std::io::Read;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use log::info;

use esp_idf_hal::gpio::Pull;

use ecotiter_fw::config;
use ecotiter_fw::infrastructure::drivers::adc::AdcDriver;
use ecotiter_fw::infrastructure::drivers::led::Led;
use ecotiter_fw::infrastructure::drivers::limitswitch::{LimitSwitch, STOP_EMPTY, STOP_FULL};
use ecotiter_fw::infrastructure::drivers::onewire;

#[allow(clippy::expect_used, clippy::too_many_lines)]
fn main() {
    use core::fmt::Write as CoreWrite;
    use ecotiter_fw::application::command::{CommandEnvelope, CommandResponse, HandlerContext};
    use ecotiter_fw::domain::burette::{BuretteCommand, BuretteState};
    use ecotiter_fw::domain::channels::CommandWithId;
    use ecotiter_fw::domain::motor_state;
    use ecotiter_fw::domain::types::{Direction, Ml, TransportMode, ValvePosition};
    use ecotiter_fw::infrastructure::drivers::stepper::RmtStepper;
    use ecotiter_fw::infrastructure::drivers::valve::Valve;
    use ecotiter_fw::infrastructure::network::http_server::HttpServer;
    use ecotiter_fw::infrastructure::network::wifi::WifiManager;
    use ecotiter_fw::interface::broadcast::{BroadcastEvent, BuretteBroadcast};
    use ecotiter_fw::stepper::ramp::{compute_ramp, RampConfig};
    use esp_idf_svc::eventloop::EspSystemEventLoop;
    use esp_idf_svc::nvs::EspDefaultNvsPartition;
    use std::sync::mpsc;
    use std::sync::mpsc::TryRecvError;

    /// Transport state machine: USB vs BLE priority.
    const fn transport_sm(usb_alive: bool, ble_connected: bool) -> TransportMode {
        if usb_alive {
            TransportMode::UsbActive
        } else if ble_connected {
            TransportMode::BleConnected
        } else {
            TransportMode::BleAdvertising
        }
    }

    esp_idf_sys::link_patches();

    // Regression guard: verify heap integrity immediately after startup.
    // Catches stack overflow / BSS corruption before any heap allocation.
    ecotiter_fw::esp_safe::check_heap_integrity();

    ecotiter_fw::esp_safe::disable_wdt();
    ecotiter_fw::esp_safe::suppress_httpd_txrx_logs();

    ecotiter_fw::logger::init();

    log::info!("=== EcoTiter firmware ===");

    let peripherals = esp_idf_hal::peripherals::Peripherals::take().expect("Peripherals::take()");

    let (free, largest) = ecotiter_fw::esp_safe::heap_stats();
    log::info!(
        "Heap: free={} KB, largest={} KB",
        free / 1024,
        largest / 1024
    );

    // ── Lightweight hardware drivers (main task, 32 KB stack) ──

    // ADC (pH electrode on GPIO34, ADC1_CH6)
    let mut adc =
        AdcDriver::new(peripherals.adc1, peripherals.pins.gpio34).expect("AdcDriver::new()");

    // Status LED (GPIO2)
    let mut led = Led::new(peripherals.pins.gpio2.degrade_output()).expect("Led::new()");

    // Limit switches: FULL (GPIO32, pull-down) and EMPTY (GPIO35, floating)
    let _limit_full = LimitSwitch::new(peripherals.pins.gpio32, Pull::Down, &STOP_FULL)
        .expect("LimitSwitch FULL (GPIO32)");

    let _limit_empty = LimitSwitch::new(peripherals.pins.gpio35, Pull::Floating, &STOP_EMPTY)
        .expect("LimitSwitch EMPTY (GPIO35)");

    // ── Valve (GPIO14) ──────────────────────────────────────────
    let valve = Valve::new(peripherals.pins.gpio14.degrade_output()).expect("Valve::new()");
    ecotiter_fw::infrastructure::drivers::valve::global_valve_init(valve);
    info!("Valve: init OK");

    // ── RMT Stepper (STEP=GPIO25, DIR=GPIO26, EN=GPIO27) ────────
    let mut stepper = RmtStepper::new(
        peripherals.pins.gpio25.degrade_output(),
        peripherals.pins.gpio26.degrade_output(),
        peripherals.pins.gpio27.degrade_output(),
    )
    .expect("RmtStepper::new()");
    stepper.set_stop_flag(&STOP_FULL);
    info!("RmtStepper: init OK");

    // ── Response channel for two-phase protocol ─────────────────
    let (response_tx, _response_rx) =
        mpsc::sync_channel::<(u64, CommandResponse)>(config::MAX_PENDING_RESPONSES);

    // ── UART reader channel ─────────────────────────────────────
    // Thread reads stdin (blocking), sends bytes to main loop (non-blocking drain).
    let (uart_tx, uart_rx) = mpsc::channel::<heapless::Vec<u8, 64>>();
    {
        let _ = std::thread::Builder::new()
            .stack_size(4096)
            .name("uart".into())
            .spawn(move || {
                let mut buf = [0u8; 64];
                loop {
                    match std::io::stdin().read(&mut buf) {
                        Ok(n) if n > 0 => {
                            let mut bytes = heapless::Vec::<u8, 64>::new();
                            let _ = bytes.extend_from_slice(&buf[..n]); // safe: n ≤ 64
                            if uart_tx.send(bytes).is_err() {
                                break; // channel closed
                            }
                        }
                        _ => {
                            // read returned 0 or error — sleep briefly before retry
                            std::thread::sleep(Duration::from_millis(10));
                        }
                    }
                }
            });
    }

    // ── Resources shared with Owner Thread ──
    #[allow(clippy::expect_used)]
    let channels: &'static ecotiter_fw::domain::channels::SystemChannels = Box::leak(Box::new(
        ecotiter_fw::domain::channels::SystemChannels::new(),
    ));
    #[allow(clippy::expect_used)]
    let cal_config: &'static ecotiter_fw::domain::calibration::CalibrationConfig = Box::leak(
        Box::new(ecotiter_fw::domain::calibration::CalibrationConfig::new()),
    );

    // Resources for Owner Thread
    let modem = peripherals.modem;
    let sys_loop = EspSystemEventLoop::take().expect("System event loop");
    let nvs = EspDefaultNvsPartition::take().expect("NVS partition");
    let ble_active = Arc::new(AtomicBool::new(false));
    let ble_active_clone = Arc::clone(&ble_active);

    // Channel: wifi_mgr from Owner Thread → main
    let (wifi_tx, wifi_rx) = std::sync::mpsc::channel();

    // BLE init (bounded sync_channel for command queue)
    let (ble_cmd_tx, ble_cmd_rx) =
        mpsc::sync_channel::<CommandEnvelope>(ecotiter_fw::domain::memory::BLE_CMD_QUEUE_SIZE);

    let mut ble_mgr = ecotiter_fw::infrastructure::network::ble::BleManager::new(ble_cmd_tx);

    // Channel: ble_mgr from Owner Thread → main
    let (ble_mgr_tx, ble_mgr_rx) =
        std::sync::mpsc::channel::<ecotiter_fw::infrastructure::network::ble::BleManager>();

    // ── Temperature thread (DS18B20 on GPIO33) ───────────────────
    {
        let gpio33 = peripherals.pins.gpio33;
        info!("DS18B20: software bitbang on GPIO33");
        let _ = std::thread::Builder::new()
            .stack_size(config::TEMP_THREAD_STACK)
            .name("temp".into())
            .spawn(move || {
                info!("Temperature thread started");
                let mut bus = match onewire::OneWireBus::new(gpio33) {
                    Ok(b) => b,
                    Err(e) => {
                        log::error!("OneWireBus::new() failed: {e:?}");
                        return;
                    }
                };
                loop {
                    if let Some(temp) = onewire::read_sensor(&mut bus) {
                        log::info!("Temperature: {temp:.1}°C");
                    }
                    std::thread::sleep(Duration::from_millis(config::TEMP_READ_INTERVAL_MS));
                }
            });
    }

    // ── HOMING SEQUENCE ──────────────────────────────────────────
    // Runs before motor task spawn. Uses the stepper directly with a stop flag.
    // Includes a wall-clock timeout of `HOMING_TIMEOUT_MS`.
    {
        use std::time::Instant;

        info!("Homing: starting");
        ecotiter_fw::infrastructure::drivers::valve::set_global_valve_position(
            ValvePosition::Input,
        );
        stepper.set_direction(Direction::LiqIn);

        let nominal_steps = ecotiter_fw::domain::calibration::volume_to_steps(
            Ml(cal_config.nominal_vol),
            cal_config.steps_per_ml,
        )
        .abs()
        .min(config::HOMING_MAX_STEPS);
        let ramp_cfg = RampConfig::new(
            config::RAMP_ACCEL_STEPS,
            config::RAMP_DECEL_STEPS,
            config::HOMING_SPEED_HZ,
            config::STEPPER_MIN_HZ,
        );
        let intervals = compute_ramp(nominal_steps, &ramp_cfg);
        let motor_ctx = ecotiter_fw::domain::context::MotorContext;

        let homing_start = Instant::now();
        let timeout = Duration::from_millis(config::HOMING_TIMEOUT_MS);

        let result = stepper.move_steps_intervals(&motor_ctx, &intervals);
        let elapsed = homing_start.elapsed();

        match (&result, elapsed < timeout) {
            (Ok(()) | Err(ecotiter_fw::errors::StepperError::LimitSwitchReached), _) => {
                info!("Homing complete — limit switch reached (elapsed={elapsed:?})");
                motor_state::CURRENT_POSITION.store(
                    nominal_steps.cast_signed(),
                    std::sync::atomic::Ordering::Release,
                );
                motor_state::set_current_volume_ml(cal_config.nominal_vol);
                motor_state::set_burette_state_tag(&BuretteState::Idle);
            }
            (Err(_), true) => {
                log::error!("Homing failed (elapsed={elapsed:?})");
                let _ = stepper.emergency_stop();
                motor_state::set_burette_state_tag(&BuretteState::Error);
            }
            _ => {
                log::error!("Homing timed out (elapsed={elapsed:?} > timeout={timeout:?})");
                let _ = stepper.emergency_stop();
                motor_state::set_burette_state_tag(&BuretteState::Error);
            }
        }
        stepper.clear_stop_flag();
    }

    // ── Spawn motor task ─────────────────────────────────────────
    // The stepper is MOVED into the task — main no longer owns it.
    ecotiter_fw::motor_task::spawn(stepper, channels, cal_config, response_tx);
    info!("Motor task: spawned");

    // ── Owner thread: WiFi + HTTP + BLE (32 KB stack) ──────────
    {
        let wifi_tx = wifi_tx;
        let ble_mgr_tx = ble_mgr_tx;
        let _ = std::thread::Builder::new()
            .stack_size(config::NET_OWNER_STACK)
            .name("net_owner".into())
            .spawn(move || {
                info!("Network owner: WiFi + HTTP + BLE init on 32 KB stack");

                let wifi_mgr = WifiManager::new(modem, sys_loop, Some(nvs), ble_active_clone)
                    .expect("WifiManager::new()");

                let wifi_mgr = Arc::new(std::sync::Mutex::new(wifi_mgr));
                let wifi_mgr_for_init = Arc::clone(&wifi_mgr);
                let wifi_mgr_for_http = Arc::clone(&wifi_mgr);
                wifi_tx.send(wifi_mgr).expect("send wifi_mgr to main");

                // Init WiFi FIRST so HTTP server has correct IP/routing
                if let Ok(mut wifi) = wifi_mgr_for_init.try_lock() {
                    wifi.init();
                }
                drop(wifi_mgr_for_init);

                let _http_server = HttpServer::new(wifi_mgr_for_http).expect("HttpServer::new()");

                // Init BLE after HTTP (DRAM still mostly pristine)
                match ble_mgr.init() {
                    Ok(()) => info!("BLE: init OK"),
                    Err(e) => log::error!("BLE init failed: {e:?}"),
                }
                let _ = ble_mgr_tx.send(ble_mgr);

                let watermark = ecotiter_fw::esp_safe::stack_watermark();
                info!("Network owner: HttpServer started (stack watermark: {watermark} bytes)");

                loop {
                    std::thread::sleep(Duration::from_hours(1));
                }
            });
    }

    // ── Получаем wifi_mgr из Owner Thread ──
    let wifi_mgr = loop {
        match wifi_rx.try_recv() {
            Ok(mgr) => break mgr,
            Err(TryRecvError::Empty) => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(TryRecvError::Disconnected) => {
                log::error!("FATAL: wifi_mgr channel disconnected during init");
                std::process::exit(1);
            }
        }
    };
    log::info!("Main: received wifi_mgr, entering event loop");

    // ── Получаем ble_mgr из Owner Thread ──
    let mut ble_mgr = loop {
        match ble_mgr_rx.try_recv() {
            Ok(mgr) => break mgr,
            Err(TryRecvError::Empty) => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(TryRecvError::Disconnected) => {
                log::error!("FATAL: ble_mgr channel disconnected during init");
                std::process::exit(1);
            }
        }
    };
    info!("Main: ble_mgr received");

    // ── Dispatch context ─────────────────────────────────────────
    let dispatch_ctx = HandlerContext {
        channels,
        cal_config,
        response_tx: &channels.response_tx,
    };

    // Serial reader for USB command processing
    let mut serial_reader = ecotiter_fw::interface::serial::SerialReader::new();
    let mut cmd_buf: heapless::Vec<u8, { ecotiter_fw::domain::memory::MAX_COMMAND_SIZE }> =
        heapless::Vec::new();

    // Pending operations manager for command watchdog
    let mut pending_ops = motor_state::PendingOpsManager::new();

    // ── Main loop ───────────────────────────────────────────────
    let mut tick_count: u64 = 0;

    loop {
        // Read ADC (non-blocking, ~30 µs)
        if let Ok(mv) = adc.read_raw_mv() {
            tick_count += 1;
            let ts_ms = tick_count * config::MAIN_LOOP_TICK_MS;

            // ─── UART polling — drain reader thread channel ────
            match uart_rx.try_recv() {
                Ok(bytes) => {
                    for &b in &bytes {
                        if serial_reader.push_byte(b, &mut cmd_buf) {
                            // Complete line received — parse and dispatch
                            let line = core::str::from_utf8(cmd_buf.as_slice()).unwrap_or("");
                            let trimmed = line.trim();
                            if !trimmed.is_empty() {
                                match serde_json::from_str::<CommandEnvelope>(trimmed) {
                                    Ok(envelope) => {
                                        let id = envelope.id;
                                        match ecotiter_fw::application::dispatch::dispatch(
                                            &dispatch_ctx,
                                            &envelope.cmd,
                                            id,
                                        ) {
                                            Ok(response) => {
                                                // Track AckThen responses in pending ops
                                                if matches!(
                                                    response,
                                                    CommandResponse::AckThen { .. }
                                                ) {
                                                    let transport =
                                                        ecotiter_fw::domain::types::TransportSource::Usb;
                                                    let _ = pending_ops.push(
                                                        motor_state::PendingOpEntry {
                                                            id,
                                                            transport,
                                                            started_at_ms: ts_ms,
                                                        },
                                                    );
                                                }
                                                let json = response.serialize();
                                                if !json.is_empty() {
                                                    info!("UART: send response id={id}");
                                                    println!("{json}");
                                                }
                                            }
                                            Err(e) => {
                                                log::error!("UART: dispatch error id={id}: {e:?}");
                                            }
                                        }
                                    }
                                    Err(e) => {
                                        log::error!("UART: JSON parse error: {e:?}");
                                    }
                                }
                            }
                            cmd_buf.clear();
                        }
                    }
                    // Touch serial activity timestamp
                    ecotiter_fw::interface::serial::serial_touch();
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    log::error!("UART reader thread disconnected");
                }
            }

            // ─── Railway: drain response_rx ────────────────────
            match channels.response_rx.try_recv() {
                Ok((resp_id, resp)) => {
                    let json = resp.serialize();
                    if !json.is_empty() {
                        info!("Response: id={resp_id}");
                        println!("{json}");
                        // Remove from pending ops if tracked
                        pending_ops.remove(resp_id);
                        // Also could send via BLE notify
                    }
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    log::error!("response_rx disconnected");
                }
            }

            // ─── Command watchdog ─────────────────────────────
            {
                let now_ms = tick_count * config::MAIN_LOOP_TICK_MS;
                let expired = pending_ops.watchdog_check(now_ms, config::WATCHDOG_CMD_TIMEOUT_MS);
                for entry in expired {
                    log::warn!(
                        "Watchdog: cmd id={} expired, sending emergency stop",
                        entry.id
                    );
                    // Send EmergencyStop to motor task
                    let _ = channels.cmd_tx.send(CommandWithId {
                        cmd: BuretteCommand::EmergencyStop,
                        id: entry.id,
                    });
                    // Send timeout error response
                    let resp = CommandResponse::Error {
                        id: entry.id,
                        message: "watchdog_timeout",
                    };
                    let json = resp.serialize();
                    println!("{json}");
                }
            }

            // ─── Network process calls ────────────────────────

            // Process WiFi (DNS poll + reconnect timer) — non-blocking
            if let Ok(mut wifi) = wifi_mgr.try_lock() {
                wifi.process();
            }

            // Process BLE zombie defense — non-blocking
            ble_mgr.process();

            // Drain BLE command queue — non-blocking
            match ble_cmd_rx.try_recv() {
                Ok(envelope) => {
                    let id = envelope.id;
                    match ecotiter_fw::application::dispatch::dispatch(
                        &dispatch_ctx,
                        &envelope.cmd,
                        id,
                    ) {
                        Ok(response) => {
                            let json = response.serialize();
                            if !json.is_empty() {
                                info!("BLE: response id={id}");
                                // TODO: send via BLE notify
                            }
                        }
                        Err(e) => {
                            log::error!("BLE: dispatch error id={id}: {e:?}");
                        }
                    }
                    info!("BLE: processed command id={}", envelope.id);
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    log::error!("BLE: command channel disconnected");
                }
            }

            // ─── Push status broadcast (every ~300ms) ─────────
            if ecotiter_fw::application::scheduler::should_broadcast() {
                let event = BroadcastEvent {
                    ts: ts_ms,
                    temp: onewire::temp_celsius(),
                    mv: mv.cast_signed(),
                    vlv: ecotiter_fw::infrastructure::drivers::valve::get_global_valve_position(),
                    brt: BuretteBroadcast {
                        sts: motor_state::get_broadcast_sts(),
                        vl: motor_state::get_current_volume_ml(),
                        spd: 0.0, // TODO: compute from motor speed
                    },
                };
                let d = ecotiter_fw::interface::broadcast::serialize_broadcast(&event);
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "status", &d,
                );
            }

            // debug broadcast
            {
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let motor_busy = motor_state::MOTOR_BUSY.load(Ordering::Acquire);
                let _ = write!(
                    d,
                    r#"{{"adc":{{"raw_mv":{mv}}},"usbSerialConnected":{},"bleConnected":{},"stepperDrv":{{"isConnected":true,"otpw":false,"ot":false,"motor":{{"stallGuard":{{"value":null,"isStalled":false,"threshold":null}},"isMoving":{}}}}},"#,
                    // Root { } closed by second write_buretteSteps part
                    ecotiter_fw::interface::serial::is_usb_alive(config::USB_ALIVE_TIMEOUT_MS),
                    ble_mgr.is_connected(),
                    motor_busy,
                );
                let _ = write!(
                    d,
                    r#""buretteSteps":{{"taken":{}}}}}"#,
                    motor_state::CURRENT_POSITION.load(Ordering::Acquire),
                );
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "debug", &d,
                );
            }

            // limitsw — periodic push (~1s)
            if tick_count.is_multiple_of(config::WS_LIMITSW_INTERVAL_TICKS) {
                let full = STOP_FULL.load(Ordering::Acquire);
                let empty = STOP_EMPTY.load(Ordering::Acquire);
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let _ = write!(d, r#"{{"full":{full},"empty":{empty}}}"#);
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "limitsw", &d,
                );
                // Clear flags after reading
                STOP_FULL.store(false, Ordering::Release);
                STOP_EMPTY.store(false, Ordering::Release);
            }

            // Transport state machine with real USB detection
            let usb_alive =
                ecotiter_fw::interface::serial::is_usb_alive(config::USB_ALIVE_TIMEOUT_MS);
            let ble_connected = ble_mgr.is_connected();
            let mode = transport_sm(usb_alive, ble_connected);
            led.set_transport_mode(mode);

            // Restart check via global flag (set by captive portal handler in owner thread)
            if ecotiter_fw::infrastructure::network::http_server::G_RESTART_PENDING
                .load(Ordering::Acquire)
            {
                log::info!("WiFi configured, restarting...");
                std::thread::sleep(Duration::from_millis(100));
                ecotiter_fw::esp_safe::restart();
            }

            // Log raw and calibrated every ~1 second (100 ticks × 10 ms)
            if tick_count.is_multiple_of(config::LOG_INTERVAL_TICKS) {
                log::info!("ADC raw: {} mV, calibrated: {} mV", mv, adc.calibrated_mv());
                if let Some(temp) = onewire::temp_celsius() {
                    log::info!("Temperature: {temp:.1} °C");
                } else {
                    log::info!("Temperature: null");
                }
            }

            // Stack watermark monitoring every ~10 seconds (1000 ticks × 10 ms)
            if tick_count.is_multiple_of(1000) {
                let wm = ecotiter_fw::esp_safe::stack_watermark();
                log::info!("Main loop stack watermark: {wm} bytes free");
                if wm < 2048 {
                    log::error!(
                        "Main stack critically low ({wm} bytes) — increase CONFIG_ESP_MAIN_TASK_STACK_SIZE"
                    );
                }
            }
        }

        // Advance the LED blink state machine
        led.process(config::MAIN_LOOP_TICK_MS);

        std::thread::sleep(Duration::from_millis(config::MAIN_LOOP_TICK_MS));
    }
}
