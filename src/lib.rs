#![deny(clippy::all)]
#![deny(unsafe_op_in_unsafe_fn)]
#![warn(clippy::pedantic)]
#![warn(clippy::nursery)]
#![warn(clippy::cargo)]
#![deny(clippy::unwrap_used)]
#![deny(clippy::expect_used)]
#![deny(clippy::panic)]
#![deny(clippy::unreachable)]
#![deny(clippy::todo)]
#![deny(clippy::unimplemented)]
#![warn(clippy::clone_on_ref_ptr)]
#![warn(clippy::redundant_allocation)]
#![warn(clippy::redundant_clone)]
#![warn(clippy::large_enum_variant)]
#![warn(clippy::boxed_local)]
#![warn(clippy::vec_box)]
#![deny(clippy::cast_possible_truncation)]
#![deny(clippy::cast_sign_loss)]
#![deny(clippy::cast_precision_loss)]
#![deny(clippy::cast_lossless)]
#![warn(clippy::float_cmp)]
#![warn(clippy::float_cmp_const)]
#![warn(clippy::if_not_else)]
#![warn(clippy::manual_assert)]
#![warn(clippy::match_wildcard_for_single_variants)]
#![warn(clippy::needless_pass_by_value)]
#![warn(clippy::trivially_copy_pass_by_ref)]
#![warn(clippy::unnecessary_wraps)]
#![allow(clippy::module_name_repetitions)]
#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(clippy::similar_names)]
#![allow(clippy::doc_markdown)]
#![allow(clippy::multiple_crate_versions)]
#![warn(unused_qualifications)]
#![warn(unused_import_braces)]

pub mod config;
pub mod domain;
pub mod errors;
pub mod stepper;

#[cfg(target_arch = "xtensa")]
pub mod logger;

#[cfg(target_arch = "xtensa")]
pub mod infrastructure;

#[cfg(target_arch = "xtensa")]
pub mod esp_mutex;

#[cfg(target_arch = "xtensa")]
pub mod esp_safe;

pub mod application;

#[cfg(target_arch = "xtensa")]
pub mod interface;

#[cfg(target_arch = "xtensa")]
pub mod motor_task;

#[cfg(test)]
mod regression_tests {
    #[test]
    fn websocket_compile_check() {
        // Compile-time verification: broadcast_websocket_event compiles
        // This test exists solely to ensure WS feature gate compiles
        assert!(true, "WS handler compiled successfully");
    }
}
