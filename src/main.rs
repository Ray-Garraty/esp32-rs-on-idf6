#![deny(clippy::unwrap_used, clippy::expect_used)]

use std::sync::atomic::AtomicBool;
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
    ecotiter_fw::logger::init();

    unsafe {
        // Safety: called once at boot, before any task uses WDT.
        // No task depends on hardware watchdog after this point.
        esp_idf_sys::esp_task_wdt_deinit();

        // Safety: c"httpd_txrx" is a valid null-terminated C-string literal.
        // esp_log_level_set only modifies a global int, no side effects.
        esp_idf_sys::esp_log_level_set(
            c"httpd_txrx".as_ptr(),
            esp_idf_sys::esp_log_level_t_ESP_LOG_ERROR,
        );

        // Brownout detector is disabled via CONFIG_BROWNOUT_DET=n in
        // sdkconfig.defaults (handles it at the Kconfig/ROM level).
    }

    log::info!("=== EcoTiter firmware ===");

    let peripherals = esp_idf_hal::peripherals::Peripherals::take().expect("Peripherals::take()");

    // Boot-time heap report
    // Safety: esp_get_free_heap_size and heap_caps_get_largest_free_block
    // are ESP-IDF FFI calls that only read hardware registers — no side
    // effects. Safe to call after FreeRTOS scheduler init (main context).
    unsafe {
        let free = esp_idf_sys::esp_get_free_heap_size();
        let largest =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DEFAULT);
        log::info!(
            "Heap: free={} KB, largest={} KB",
            free / 1024,
            largest / 1024
        );
    }

    // ── Initialise hardware drivers ──────────────────────────────

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

    // ── Network subsystem initialisation ─────────────────────────

    // Box::leak to obtain &'static refs for EspHttpServer fn_handler 'static bound.
    // This leaks ~few hundred bytes once at boot — acceptable for firmware that
    // runs until power-off.
    #[allow(clippy::expect_used)]
    let channels: &'static ecotiter_fw::domain::channels::SystemChannels = Box::leak(Box::new(
        ecotiter_fw::domain::channels::SystemChannels::new(),
    ));
    #[allow(clippy::expect_used)]
    let cal_config: &'static ecotiter_fw::domain::calibration::CalibrationConfig = Box::leak(
        Box::new(ecotiter_fw::domain::calibration::CalibrationConfig::new()),
    );

    // WiFi init
    let sys_loop = EspSystemEventLoop::take().expect("System event loop");
    let nvs = EspDefaultNvsPartition::take().expect("NVS partition");
    let ble_active = Arc::new(AtomicBool::new(false));

    #[allow(clippy::expect_used)]
    let mut wifi_mgr = WifiManager::new(peripherals.modem, sys_loop, Some(nvs), ble_active)
        .expect("WifiManager::new()");
    wifi_mgr.init(); // Try STA → fall back to AP
    let wifi_mgr = Arc::new(Mutex::new(wifi_mgr));

    // BLE init (bounded sync_channel for command queue)
    let (ble_cmd_tx, ble_cmd_rx) =
        mpsc::sync_channel::<CommandEnvelope>(ecotiter_fw::domain::memory::BLE_CMD_QUEUE_SIZE);

    let mut ble_mgr = ecotiter_fw::infrastructure::network::ble::BleManager::new(ble_cmd_tx);

    // BLE init is gated — enable after NimBLE testing
    // BLE init is deferred because esp32-nimble needs a local patch for IDF v6
    // (see AGENTS.md "Common Issues" — add all(esp_idf_version_major = "6") to
    // two cfg_if! blocks in ble_characteristic.rs). Once patched, uncomment below:
    // let _ = ble_mgr.init();

    // SSE channel: main loop pushes events, HTTP handler blocks and sends via httpd_resp_send_chunk
    let sse_tx = Arc::new(Mutex::new(
        None::<
            mpsc::SyncSender<heapless::String<{ ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE }>>,
        >,
    ));

    // HTTP Server
    #[allow(clippy::expect_used)]
    let http_server = HttpServer::new(wifi_mgr.clone(), sse_tx.clone()).expect("HttpServer::new()");

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

            // SSE push via channel
            if let Ok(guard) = sse_tx.try_lock() {
                if let Some(tx) = guard.as_ref() {
                    let mut event_data: heapless::String<
                        { ecotiter_fw::domain::memory::MAX_RESPONSE_SIZE },
                    > = heapless::String::new();
                    let _ = event_data.push_str(
                        r#"{"ts":0,"temp":null,"mv":0,"vlv":"in","brt":{"sts":"idle","vl":0.0,"spd":0.0}}"#,
                    );
                    if tx.try_send(event_data).is_err() {
                        // Channel full or disconnected — non-critical, just drop
                    }
                }
            }

            // Transport state machine
            let usb_alive = false; // TODO: Phase 5 — check USB serial activity timestamp
            let ble_connected = ble_mgr.is_connected();
            let mode = transport_sm(usb_alive, ble_connected);
            led.set_transport_mode(mode);

            // Restart check
            if http_server.restart_pending() {
                log::info!("WiFi configured, restarting...");
                std::thread::sleep(Duration::from_millis(100));
                // Safety: esp_restart() is an IDF function that resets the chip.
                // Safe to call here: all state is saved to NVS before this point,
                // and this is the last operation before the CPU resets.
                unsafe {
                    esp_idf_sys::esp_restart();
                }
            }

            // Log raw and calibrated every ~1 second (100 ticks × 10 ms)
            #[allow(clippy::manual_is_multiple_of)]
            if tick_count % 100 == 0 {
                log::info!("ADC raw: {} mV, calibrated: {} mV", mv, adc.calibrated_mv());
                if let Some(temp) = onewire::temp_celsius() {
                    log::info!("Temperature: {temp:.1} °C");
                } else {
                    log::info!("Temperature: null");
                }
            }
        }

        // Advance the LED blink state machine
        led.process(config::MAIN_LOOP_TICK_MS);

        std::thread::sleep(Duration::from_millis(config::MAIN_LOOP_TICK_MS));
    }
}
