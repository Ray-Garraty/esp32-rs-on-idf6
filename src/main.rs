#![deny(clippy::unwrap_used, clippy::expect_used)]
#![forbid(unsafe_code)]
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use log::info;

use esp_idf_hal::gpio::Pull;

use ecotiter_fw::config;
use ecotiter_fw::diag;
use ecotiter_fw::infrastructure::drivers::adc::AdcDriver;
use ecotiter_fw::infrastructure::drivers::led::Led;
use ecotiter_fw::infrastructure::drivers::limitswitch::{LimitSwitch, STOP_EMPTY, STOP_FULL};
use ecotiter_fw::infrastructure::drivers::onewire;

/// Register a panic hook that dumps the black box and stack watermarks to UART
/// before the ESP32 resets. No heap allocations — uses raw `write(1, ...)`.
fn setup_panic_hook() {
    use core::fmt::Write as CoreWrite;
    struct UartWriter;
    impl CoreWrite for UartWriter {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            ecotiter_fw::esp_safe::panic_write_str(s);
            Ok(())
        }
    }
    std::panic::set_hook(Box::new(|panic_info| {
        let mut w = UartWriter;
        let _ = write!(&mut w, "\n!!! PANIC");
        if let Some(loc) = panic_info.location() {
            let _ = write!(&mut w, " at {}:{}", loc.file(), loc.line());
        }
        let _ = writeln!(&mut w, " !!!");
        if let Some(s) = panic_info.payload().downcast_ref::<&str>() {
            let _ = writeln!(&mut w, "Message: {s}");
        }
        diag::black_box::dump(&mut w);
        diag::stack_monitor::emergency_dump(&mut w);
        let _ = writeln!(&mut w, "!!! PANIC END !!!");
    }));
}

#[allow(clippy::expect_used, clippy::too_many_lines)]
fn main() {
    use core::fmt::Write as CoreWrite;
    use ecotiter_fw::application::command::{CommandEnvelope, CommandResponse, HandlerContext};
    use ecotiter_fw::domain::burette::BuretteCommand;
    use ecotiter_fw::domain::channels::CommandWithId;
    use ecotiter_fw::domain::motor_state;
    use ecotiter_fw::domain::types::{Direction, TransportMode, ValvePosition};
    use ecotiter_fw::infrastructure::drivers::stepper::RmtStepper;
    use ecotiter_fw::infrastructure::drivers::valve::Valve;
    use ecotiter_fw::infrastructure::network::http_server;
    use ecotiter_fw::infrastructure::network::http_server::HttpServer;
    use ecotiter_fw::infrastructure::network::wifi::WifiManager;
    use ecotiter_fw::infrastructure::storage::nvs;
    use ecotiter_fw::interface::broadcast::{BroadcastEvent, BuretteBroadcast};
    use esp_idf_svc::eventloop::EspSystemEventLoop;
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

    static LAST_TRANSPORT: core::sync::atomic::AtomicU8 = core::sync::atomic::AtomicU8::new(0xFF);

    esp_idf_sys::link_patches();

    // Regression guard: verify heap integrity immediately after startup.
    // Catches stack overflow / BSS corruption before any heap allocation.
    ecotiter_fw::esp_safe::check_heap_integrity();

    ecotiter_fw::esp_safe::disable_wdt();
    ecotiter_fw::esp_safe::suppress_httpd_txrx_logs();

    ecotiter_fw::logger::init();

    setup_panic_hook();
    diag::init();

    log::info!("=== esp32-rs-on-idf6 ===");

    let peripherals = esp_idf_hal::peripherals::Peripherals::take().expect("Peripherals::take()");

    let (free, largest, dma_largest) = ecotiter_fw::esp_safe::heap_stats();
    log::info!(
        "Heap: free={} KB, largest={} KB, DMA_largest={} KB",
        free / 1024,
        largest / 1024,
        dma_largest / 1024,
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
    #[allow(clippy::expect_used)]
    let nvs = nvs::nvs_init().expect("NVS init");
    let ble_active = Arc::new(AtomicBool::new(false));
    let ble_active_clone = Arc::clone(&ble_active);

    // Pre-WiFi DMA heap diagnostic
    {
        let (free, largest, dma_largest) = ecotiter_fw::esp_safe::heap_stats();
        log::info!(
            "Pre-WiFi heap: free={}K, largest={}K, DMA_largest={}K",
            free / 1024,
            largest / 1024,
            dma_largest / 1024,
        );
    }

    // Channel: wifi_mgr from Owner Thread → main
    let (wifi_tx, wifi_rx) = std::sync::mpsc::channel();

    // BLE init (bounded sync_channel for command queue)
    let (ble_cmd_tx, ble_cmd_rx) =
        mpsc::sync_channel::<CommandEnvelope>(ecotiter_fw::domain::memory::BLE_CMD_QUEUE_SIZE);

    let mut ble_mgr = ecotiter_fw::infrastructure::network::ble::BleManager::new(ble_cmd_tx);

    // Channel: ble_mgr from Owner Thread → main
    let (ble_mgr_tx, ble_mgr_rx) =
        std::sync::mpsc::channel::<ecotiter_fw::infrastructure::network::ble::BleManager>();

    // ── HOMING runs inside motor task ──
    // Set valve and direction for homing
    ecotiter_fw::infrastructure::drivers::valve::set_global_valve_position(ValvePosition::Input);
    stepper.set_direction(Direction::LiqIn);
    // Attach limit switch stop flag so homing stops on contact
    stepper.set_stop_flag(&STOP_FULL);

    // ── Owner thread: WiFi + HTTP + BLE (before stack-heavy threads) ──
    // Spawn net_owner FIRST so WiFi init gets pristine DRAM before
    // motor (16KB), temp (16KB), and UART (8KB) stacks fragment the heap.
    diag::stack_monitor::register_thread(diag::stack_monitor::NET_OWNER, "net_owner");
    {
        let wifi_tx = wifi_tx;
        let ble_mgr_tx = ble_mgr_tx;
        let _ = std::thread::Builder::new()
            .stack_size(config::NET_OWNER_STACK)
            .name("net_owner".into())
            .spawn(move || {
                diag::black_box::set_thread_slot(diag::stack_monitor::NET_OWNER);
                info!("Network owner: WiFi + HTTP + BLE init on 32 KB stack");

                // Clone sys_loop before passing to WifiManager::new().
                // If EspWifi::new() fails, it drops the original sys_loop,
                // which kills lwIP's tcpip thread. The clone keeps lwIP alive
                // for HTTP server and BLE even without WiFi.
                let _sysloop_keepalive = sys_loop.clone();
                // Ensure lwIP TCP/IP thread exists before any network operation.
                // If WifiManager::new() fails due to DRAM exhaustion, esp_netif_init()
                // has already created the tcpip mbox, so HTTP and BLE can still work.
                ecotiter_fw::esp_safe::netif_init();
                let wifi_mgr = match WifiManager::new(modem, sys_loop, Some(nvs.clone()), Arc::clone(&ble_active_clone)) {
                    Ok(w) => w,
                    Err(e) => {
                        let (free, largest, dma_largest) = ecotiter_fw::esp_safe::heap_stats();
                        log::error!(
                            "WiFi init failed: {e:?}. Heap: free={}K, largest={}K, DMA_largest={}K. Running offline.",
                            free / 1024, largest / 1024, dma_largest / 1024,
                        );
                        WifiManager::offline(ble_active_clone)
                    }
                };

                let wifi_mgr = Arc::new(std::sync::Mutex::new(wifi_mgr));
                let wifi_mgr_for_init = Arc::clone(&wifi_mgr);
                let wifi_mgr_for_http = Arc::clone(&wifi_mgr);
                wifi_tx.send(wifi_mgr).expect("send wifi_mgr to main");

                // Init WiFi FIRST so HTTP server has correct IP/routing
                if let Ok(mut wifi) = wifi_mgr_for_init.try_lock() {
                    wifi.init();
                    diag::heap_snapshot::snapshot("wifi_init");
                    // Post-WiFi DMA heap diagnostic
                    let (free, largest, dma_largest) = ecotiter_fw::esp_safe::heap_stats();
                    log::info!(
                        "Post-WiFi heap: free={}K, largest={}K, DMA_largest={}K",
                        free / 1024,
                        largest / 1024,
                        dma_largest / 1024,
                    );
                }
                drop(wifi_mgr_for_init);

                let _http_server = match HttpServer::new(wifi_mgr_for_http) {
                    Ok(server) => {
                        let watermark = ecotiter_fw::esp_safe::stack_watermark();
                        info!("HTTP: server started (stack watermark: {watermark} bytes)");
                        Some(server)
                    }
                    Err(e) => {
                        log::error!("HTTP: server init failed: {e:?}");
                        None
                    }
                };
                diag::heap_snapshot::snapshot("http_started");
                let http_ok = _http_server.is_some();
                ecotiter_fw::infrastructure::network::http_server::G_HTTP_SERVER_ALIVE
                    .store(http_ok, core::sync::atomic::Ordering::Release);

                // Init BLE after HTTP (DRAM still mostly pristine)
                match ble_mgr.init() {
                    Ok(()) => info!("BLE: init OK"),
                    Err(e) => log::error!("BLE init failed: {e:?}"),
                }
                diag::heap_snapshot::snapshot("ble_init");
                let _ = ble_mgr_tx.send(ble_mgr);

                loop {
                    std::thread::sleep(Duration::from_secs(10));
                    diag::stack_monitor::check_watermark(diag::stack_monitor::NET_OWNER);
                }
            });
    }

    // ── Receive wifi_mgr from Owner Thread ──
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

    // ── Spawn motor task ─────────────────────────────────────────
    // Spawn BEFORE BLE init, while heap has ~60KB+ free (after WiFi+HTTP,
    // before BLE consumes ~30KB). Motor/temp/uart do NOT need BLE.
    // The stepper is MOVED into the task — main no longer owns it.
    // Homing runs inside the motor task before entering command loop.
    ecotiter_fw::motor_task::spawn(stepper, channels, cal_config, response_tx);
    info!("Motor task: spawned (homing runs inside)");

    // ── Temperature thread (DS18B20 on GPIO33) ───────────────────
    {
        let gpio33 = peripherals.pins.gpio33;
        info!("DS18B20: software bitbang on GPIO33");
        diag::stack_monitor::register_thread(diag::stack_monitor::TEMP, "temp");
        let _ = std::thread::Builder::new()
            .stack_size(config::TEMP_THREAD_STACK)
            .name("temp".into())
            .spawn(move || {
                diag::black_box::set_thread_slot(diag::stack_monitor::TEMP);
                info!("Temperature thread started");
                let mut bus = match onewire::OneWireBus::new(gpio33) {
                    Ok(b) => b,
                    Err(e) => {
                        log::error!("OneWireBus::new() failed: {e:?}");
                        return;
                    }
                };
                let mut temp_tick = 0u64;
                loop {
                    temp_tick += 1;
                    if temp_tick.is_multiple_of(100) {
                        diag::stack_monitor::check_watermark(diag::stack_monitor::TEMP);
                    }
                    if let Some(temp) = onewire::read_sensor(&mut bus) {
                        log::info!("Temperature: {temp:.1}°C");
                    }
                    std::thread::sleep(Duration::from_millis(config::TEMP_READ_INTERVAL_MS));
                }
            });
    }

    // ── UART reader channel ─────────────────────────────────────
    // Thread reads stdin (blocking), sends bytes to main loop (non-blocking drain).
    //
    // Initialize UART0 VFS driver + install driver for blocking stdin reads.
    // Without this, `std::io::stdin().read()` panics because the VFS layer has
    // no driver backing fd 0 — ESP-IDF's boot console only sets up polled-mode
    // output (printf/println), not interrupt-driven input.
    ecotiter_fw::esp_safe::uart_init_stdin();
    let (uart_tx, uart_rx) = mpsc::channel::<heapless::Vec<u8, 64>>();
    diag::stack_monitor::register_thread(diag::stack_monitor::UART, "uart");
    {
        #[allow(clippy::expect_used)]
        match std::thread::Builder::new()
            .stack_size(config::UART_THREAD_STACK)
            .name("uart".into())
            .spawn(move || {
                // [INVESTIGATION] raw UART write at thread entry
                ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: entered closure\n");
                diag::black_box::set_thread_slot(diag::stack_monitor::UART);
                ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: after set_thread_slot\n");
                let mut buf = [0u8; 64];
                let mut uart_tick = 0u64;
                ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: before loop\n");
                loop {
                    uart_tick += 1;
                    if uart_tick.is_multiple_of(100) {
                        diag::stack_monitor::check_watermark(diag::stack_monitor::UART);
                    }
                    ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: calling uart_read_stdin_blocking\n");
                    match ecotiter_fw::esp_safe::uart_read_stdin_blocking(&mut buf) {
                        Ok(n) if n > 0 => {
                            let mut bytes = heapless::Vec::<u8, 64>::new();
                            let _ = bytes.extend_from_slice(&buf[..n]); // safe: n ≤ 64
                            if uart_tx.send(bytes).is_err() {
                                break; // channel closed
                            }
                        }
                        _ => {
                            // uart_read_stdin_blocking returned 0 (no data) or
                            // Err (driver not installed) — sleep briefly before retry.
                            std::thread::sleep(Duration::from_millis(10));
                        }
                    }
                }
            }) {
            Ok(_handle) => {
                // [INVESTIGATION] UART thread spawned successfully
                ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: spawn OK\n");
            }
            Err(e) => {
                // [INVESTIGATION] UART thread spawn FAILED
                ecotiter_fw::esp_safe::panic_write_str("[INVESTIGATION] UART thread: spawn FAILED\n");
                log::error!("UART thread spawn failed: {e:?}");
            }
        }
    }

    // ── Receive ble_mgr from Owner Thread ──
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
        diag::tick_watchdog::tick_begin();
        #[cfg(not(test))]
        let _loop_start = ecotiter_fw::esp_safe::micros();
        // Read ADC (non-blocking, ~30 µs)
        if let Ok(mv) = adc.read_raw_mv() {
            // Advance scheduler tick counter (used by should_broadcast())
            ecotiter_fw::application::scheduler::tick(config::MAIN_LOOP_TICK_MS);
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
                if http_server::G_HTTP_SERVER_ALIVE.load(Ordering::Acquire) {
                    ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                        "status", &d,
                    );
                }
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
                if http_server::G_HTTP_SERVER_ALIVE.load(Ordering::Acquire) {
                    ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                        "debug", &d,
                    );
                }
            }

            // limitsw — periodic push (~1s)
            if tick_count.is_multiple_of(config::WS_LIMITSW_INTERVAL_TICKS) {
                let full = STOP_FULL.load(Ordering::Acquire);
                let empty = STOP_EMPTY.load(Ordering::Acquire);
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let _ = write!(d, r#"{{"full":{full},"empty":{empty}}}"#);
                if http_server::G_HTTP_SERVER_ALIVE.load(Ordering::Acquire) {
                    ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                        "limitsw", &d,
                    );
                }
                // Clear flags after reading
                STOP_FULL.store(false, Ordering::Release);
                STOP_EMPTY.store(false, Ordering::Release);
            }

            // Transport state machine with real USB detection
            let usb_alive =
                ecotiter_fw::interface::serial::is_usb_alive(config::USB_ALIVE_TIMEOUT_MS);
            let ble_connected = ble_mgr.is_connected();
            let mode = transport_sm(usb_alive, ble_connected);
            let mode_id = match mode {
                TransportMode::UsbActive => 0u8,
                TransportMode::BleAdvertising => 1u8,
                TransportMode::BleConnected => 2u8,
            };
            let prev = LAST_TRANSPORT.swap(mode_id, core::sync::atomic::Ordering::Relaxed);
            if prev != mode_id && prev != 0xFF {
                diag::state_tracer::log_transport_transition(prev, mode_id);
            }
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

            // Heap + stack watermark monitoring every ~10 seconds (1000 ticks × 10 ms)
            if tick_count.is_multiple_of(1000) {
                let wm = ecotiter_fw::esp_safe::stack_watermark();
                let (free, largest, dma_largest) = ecotiter_fw::esp_safe::heap_stats();
                log::info!(
                    "Heap: free={}K, largest={}K, DMA_largest={}K | Stack: {wm} bytes free",
                    free / 1024,
                    largest / 1024,
                    dma_largest / 1024,
                );
                if wm < 2048 {
                    log::error!(
                        "Main stack critically low ({wm} bytes) — increase CONFIG_ESP_MAIN_TASK_STACK_SIZE"
                    );
                }
                diag::stack_monitor::check_watermark(diag::stack_monitor::MAIN);
            }

            #[cfg(not(test))]
            {
                let elapsed = ecotiter_fw::esp_safe::micros().wrapping_sub(_loop_start);
                if elapsed > config::MAIN_LOOP_TICK_MS * 1000 * 2 {
                    log::warn!(
                        "Main loop body: {elapsed}µs (limit: {}ms)",
                        config::MAIN_LOOP_TICK_MS * 2
                    );
                }
            }
        }

        diag::tick_watchdog::tick_end();

        // Advance the LED blink state machine
        led.process(config::MAIN_LOOP_TICK_MS);

        std::thread::sleep(Duration::from_millis(config::MAIN_LOOP_TICK_MS));
    }
}
