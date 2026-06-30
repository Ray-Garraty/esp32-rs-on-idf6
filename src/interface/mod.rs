//! Interface layer — external boundaries.
//!
//! Serial line reader, broadcast serialization, and REST API route handler
//! definitions. All modules are gated behind `#[cfg(target_arch = "xtensa")]`
//! because they depend on ESP-IDF infrastructure.

#[cfg(target_arch = "xtensa")]
pub mod broadcast;
#[cfg(target_arch = "xtensa")]
pub mod rest_api;
#[cfg(target_arch = "xtensa")]
pub mod serial;
