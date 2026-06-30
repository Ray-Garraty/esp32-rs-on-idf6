//! Command handler modules.
//!
//! Each submodule implements `CommandHandler` for a logical group of commands.
//! Handlers use domain types and the `HandlerContext` — no ESP-IDF imports.

pub mod burette_cal;
pub mod burette_ops;
pub mod sensors;
pub mod serial;
pub mod system;
pub mod valve;
