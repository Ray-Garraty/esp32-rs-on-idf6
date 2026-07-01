#![deny(clippy::unwrap_used, clippy::expect_used)]
#![forbid(unsafe_code)]
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
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
    use ecotiter_fw::application::command::CommandEnvelope;
    use ecotiter_fw::domain::types::TransportMode;
    use ecotiter_fw::infrastructure::network::http_server::HttpServer;
    use ecotiter_fw::infrastructure::network::wifi::WifiManager;
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
    ecotiter_fw::esp_safe::disable_wdt();
    ecotiter_fw::esp_safe::suppress_httpd_txrx_logs();
    ecotiter_fw::logger::init();

    log::info!("=== EcoTiter firmware ===");

    let peripherals = esp_idf_hal::peripherals::Peripherals::take().expect("Peripherals::take()");

    // Boot-time heap report
    let (free, largest) = ecotiter_fw::esp_safe::heap_stats();
    log::info!(
        "Heap: free={} KB, largest={} KB",
        free / 1024,
        largest / 1024
    );

    // ── Lightweight hardware drivers (main task, 16 KB stack) ──

    // ADC (pH electrode on GPIO34, ADC1_CH6)
    let mut adc =
        AdcDriver::new(peripherals.adc1, peripherals.pins.gpio34).expect("AdcDriver::new()");

    // Status LED (GPIO2)
    let mut led = Led::new(peripherals.pins.gpio2.degrade_output()).expect("Led::new()");

    // Limit switches: FULL (GPIO32, pull-down) and EMPTY (GPIO35, floating)
    let mut _limit_full = LimitSwitch::new(peripherals.pins.gpio32, Pull::Down, &STOP_FULL)
        .expect("LimitSwitch FULL (GPIO32)");

    let mut _limit_empty = LimitSwitch::new(peripherals.pins.gpio35, Pull::Floating, &STOP_EMPTY)
        .expect("LimitSwitch EMPTY (GPIO35)");

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

    // BLE init is gated — enable after NimBLE testing
    // let _ = ble_mgr.init();

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

    // ── Owner thread: WiFi + HTTP (32 KB stack) ────────────────
    {
        let wifi_tx = wifi_tx;
        let _ = std::thread::Builder::new()
            .stack_size(config::NET_OWNER_STACK)
            .name("net_owner".into())
            .spawn(move || {
                info!("Network owner: WiFi + HTTP init on 32 KB stack");

                let mut wifi_mgr = WifiManager::new(modem, sys_loop, Some(nvs), ble_active_clone)
                    .expect("WifiManager::new()");
                wifi_mgr.init();

                let wifi_mgr = Arc::new(Mutex::new(wifi_mgr));
                let wifi_mgr_for_http = Arc::clone(&wifi_mgr);
                wifi_tx.send(wifi_mgr).expect("send wifi_mgr to main");

                let _http_server = HttpServer::new(wifi_mgr_for_http).expect("HttpServer::new()");

                let watermark = ecotiter_fw::esp_safe::stack_watermark();
                log::info!(
                    "Network owner: HttpServer started (stack watermark: {watermark} bytes)"
                );

                loop {
                    std::thread::sleep(Duration::from_hours(1));
                }
            });
    }

    // ── Получаем wifi_mgr из Owner Thread ──
    // Non-blocking wait: try_recv + 10ms sleep. Init phase, before main loop — no semgrep violation.
    let wifi_mgr = loop {
        match wifi_rx.try_recv() {
            Ok(mgr) => break mgr,
            Err(TryRecvError::Empty) => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(TryRecvError::Disconnected) => {
                panic!("wifi_mgr channel disconnected");
            }
        }
    };
    log::info!("Main: received wifi_mgr, entering event loop");

    // ── Main loop ───────────────────────────────────────────────
    let mut tick_count: u64 = 0;

    loop {
        // Read ADC (non-blocking, ~30 µs)
        if let Ok(mv) = adc.read_raw_mv() {
            tick_count += 1;

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
                    let ctx = ecotiter_fw::application::command::HandlerContext {
                        channels,
                        cal_config,
                    };
                    let _response = ecotiter_fw::application::dispatch::dispatch(
                        &ctx,
                        &envelope.cmd,
                        envelope.id,
                    );
                    log::info!("BLE: processed command id={}", envelope.id);
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    log::error!("BLE: command channel disconnected");
                }
            }

            // Push events via WebSocket (non-blocking broadcast)
            {
                let ts_ms = tick_count * config::MAIN_LOOP_TICK_MS;
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let _ = write!(
                    d,
                    r#"{{"ts":{ts_ms},"temp":null,"mv":{mv},"vlv":"in","brt":{{"sts":"idle","vl":0.0,"spd":0.0}}}}"#,
                );
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "status", &d,
                );
            }

            // debug
            {
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let _ = write!(
                    d,
                    r#"{{"adc":{{"raw_mv":{mv}}},"usbSerialConnected":false,"bleConnected":false,"stepperDrv":{{"isConnected":false,"otpw":false,"ot":false,"motor":{{"stallGuard":{{"value":null,"isStalled":false,"threshold":null}},"isMoving":false}}}},"buretteSteps":{{"taken":0}}}}"#,
                );
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "debug", &d,
                );
            }

            // limitsw — periodic push
            if tick_count.is_multiple_of(config::WS_LIMITSW_INTERVAL_TICKS) {
                let mut d: heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }> =
                    heapless::String::new();
                let _ = d.push_str(r#"{"full":false,"empty":false}"#);
                ecotiter_fw::infrastructure::network::http_server::broadcast_websocket_event(
                    "limitsw", &d,
                );
            }

            // Transport state machine
            let usb_alive = false; // TODO: Phase 5 — check USB serial activity timestamp
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
