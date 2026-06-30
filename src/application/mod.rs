//! Application layer — use-case orchestration.
//!
//! Coordinates command dispatch, handler routing, state machine transitions,
//! and timing. This layer uses only domain types and traits — no ESP-IDF imports.
//! Compiles on host (x86_64) for unit testing.

pub mod command;
pub mod dispatch;
pub mod handlers;
pub mod scheduler;
pub mod state_machine;
