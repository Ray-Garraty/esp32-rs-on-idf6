# esp32-rs-on-idf6 — Development Log

Format: `YYYY-MM-DD | Phase | Type | Status | Description`

---

2026-06-30 | Phase 0 | feature | complete | Project scaffold: domain types, error hierarchy, config, logger, boot sequence
2026-06-30 | Phase 1 | feature | complete | Domain pure business logic: burette state machine, calibration math, planner, stepper ramp, system channels (138 tests)
2026-06-30 | Phase 2 | feature | complete | Infrastructure hardware drivers: RMT stepper, ADC (pH), OneWire (DS18B20), valve, limit switch, LED, NVS storage (143 tests, 2 rework cycles)
2026-06-30 | Phase 4 | feature | complete | Network subsystem: WiFi AP/STA/captive portal/DNS, BLE NUS GATT + zombie defense, HTTP server (12 routes + SSE + WebUI), transport state machine, embedded dashboard (226 tests, 2 rework cycles)
2026-06-30 | Phase 3 | feature | complete | Application layer: command dispatch (32 variants), 6 handler modules, state machine, serial reader, broadcast serialization, REST API stubs (222 tests, 0 rework cycles)
2026-07-01 | Phase 4 | bugfix | complete | ESP32 stack overflow bugfix (SW_CPU_RESET): 4-layer fix - Owner Thread (32 KB net_owner), global restart flag (G_RESTART_PENDING), stack watermark monitoring, SSE-to-WebSocket migration (227 tests, unsafe 23->22 net decrease, 0 rework cycles)
2026-07-06 | Migration | ESP32→S3 | complete | Full migration to ESP32-S3: build system, GPIO re-assignment, memory map, RMT assertion, IRAM panic handler, WDT/logger order fix, dead code cleanup; requires hardware validation
2026-07-09 | Phase 4 | bugfix | complete | Captive portal working: JSON parser fix (LL-036), HTTP redirect cleanup (302→303 See Other, 24 max_uri_handlers, root_handler), WiFi race fix + AP auto-stop, password toggle, domain::findJsonField() utility (6 tests, 4 rework cycles)
2026-07-11 | Phase 4 | bugfix | complete | mDNS hostname resolution: add idf_component.yml (espressif/mdns ^1.11.3), implement real startMdns() with mdns_init/hostname_set/service_add, add host stub + regression test, fix .gitignore for managed_components (1 test, 0 rework cycles)
