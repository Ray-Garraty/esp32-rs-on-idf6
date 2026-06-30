//! Network subsystem — WiFi, HTTP server, and BLE.
//!
//! All modules are gated behind `#[cfg(target_arch = "xtensa")]` since they
//! depend on `esp-idf-*` crates that do not exist on host build targets.

pub mod ble;
pub mod http_server;
pub mod wifi;
