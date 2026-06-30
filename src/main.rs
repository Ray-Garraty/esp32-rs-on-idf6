use std::time::Duration;

use log::info;

use esp_idf_hal::gpio::Pull;

use ecotiter_fw::config;
use ecotiter_fw::infrastructure::drivers::adc::AdcDriver;
use ecotiter_fw::infrastructure::drivers::led::Led;
use ecotiter_fw::infrastructure::drivers::limitswitch::{LimitSwitch, STOP_EMPTY, STOP_FULL};
use ecotiter_fw::infrastructure::drivers::onewire;

fn main() {
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

    // ── Main loop ───────────────────────────────────────────────
    let mut tick_count: u64 = 0;

    loop {
        // Read ADC (non-blocking, ~30 µs)
        if let Ok(mv) = adc.read_raw_mv() {
            tick_count += 1;
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
