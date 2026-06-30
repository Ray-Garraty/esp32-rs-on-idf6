# EcoTiter Firmware — Development Log

Format: `YYYY-MM-DD | Phase | Type | Status | Description`

---

2026-06-30 | Phase 0 | feature | complete | Project scaffold: domain types, error hierarchy, config, logger, boot sequence
2026-06-30 | Phase 1 | feature | complete | Domain pure business logic: burette state machine, calibration math, planner, stepper ramp, system channels (138 tests)
2026-06-30 | Phase 2 | feature | complete | Infrastructure hardware drivers: RMT stepper, ADC (pH), OneWire (DS18B20), valve, limit switch, LED, NVS storage (143 tests, 2 rework cycles)
2026-06-30 | Phase 4 | feature | complete | Network subsystem: WiFi AP/STA/captive portal/DNS, BLE NUS GATT + zombie defense, HTTP server (12 routes + SSE + WebUI), transport state machine, embedded dashboard (226 tests, 2 rework cycles)
2026-06-30 | Phase 3 | feature | complete | Application layer: command dispatch (32 variants), 6 handler modules, state machine, serial reader, broadcast serialization, REST API stubs (222 tests, 0 rework cycles)
