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
2026-07-11 | Phase 4 | bugfix | complete | LL-038/043 cascade fix (3 crashes): (1) system event task stack overflow at IP_EVENT — async LogBuffer + 8KB event stack; (2) worker WS callback corrupts heap via httpd_ws_send_frame_async (synchronous! despite name) + LWIP send() overflows 4KB worker — deferred worker creation + 8KB worker stack. Root cause: httpd_ws_send_frame_async is synchronous, calls send_fn directly (LL-043).
2026-07-12 | Agents | refactoring | complete | Debugger subagent workflow overhaul: Pre-Flight Study (Phase 0), hypothesis falsification requirement, structured output format (## Data / ## Platform Facts / ## Hypotheses / ## Experiment / ## Conclusion / ## STUCK), inherited context from foreman (experiment table + git diff + log tail), Early Termination Gate after 3 experiments, expanded resources (project.md, coding_style.md, crash_analyzer.py), log handling rules, 16 rules from 11
2026-07-13 | Phase 4 | bugfix | complete | WiFi STA connection on boot: tryStartSTA() silently failed (init sets APSTA but no esp_wifi_start, guard missed APSTA). Rewrite tryStartSTA() blocking per-slot with event group wait, saveCredentials() with FIFO eviction (5 networks), STA-first boot flow, NVS READONLY fix, 3 regression tests (0 rework cycles)
