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

    let _peripherals = esp_idf_hal::peripherals::Peripherals::take().expect("Peripherals::take()");

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

    // AC-005 gate: require free > 150 KB and largest > 80 KB
    // (checked via log output)

    loop {
        std::thread::sleep(std::time::Duration::from_millis(
            ecotiter_fw::config::MAIN_LOOP_TICK_MS,
        ));
    }
}
