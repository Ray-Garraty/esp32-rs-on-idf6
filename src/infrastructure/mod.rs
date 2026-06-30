//! Infrastructure layer — hardware drivers and platform-specific code.
//!
//! This layer wraps ESP-IDF HAL and sys APIs. All modules are gated
//! behind `#[cfg(target_arch = "xtensa")]` since they import `esp-idf-*`
//! crates that do not exist on host build targets.
//!
//! See `docs/refs/coding_style.md §1` for layered architecture rules.
//! See `docs/refs/project.md` for pinout and hardware design.

pub mod drivers;
pub mod network;
pub mod storage;
