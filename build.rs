#![allow(
    clippy::disallowed_types,    // Vec<String> OK in build script (not hot path)
    clippy::if_not_else,         // build script logic fine as-is
)]

use embuild as _;
use embuild::build::LinkArgs;
use std::path::{Path, PathBuf};

fn main() {
    // Suppress cfg warning for esp32-nimble IDF v6 patches (our crate only)
    println!("cargo::rustc-check-cfg=cfg(esp_idf_version_major, values(\"6\"))");

    // Only esp-idf linker args + patching are needed on xtensa targets
    let target = std::env::var("TARGET").unwrap_or_default();
    if !target.contains("xtensa") {
        return;
    }

    // Propagate linker arguments from esp-idf-sys (xtensa only — DEP_ESP_IDF_*
    // env vars are not set on host builds).
    // NOTE: lib_name must be UPPERCASE to match cargo's env var naming.
    LinkArgs::output_propagated("ESP_IDF").unwrap_or_else(|e| {
        println!("cargo:warning=LinkArgs::output_propagated failed: {e}");
    });

    // Patch esp32-nimble for IDF v6
    if let Some(file) = find_esp32_nimble_source() {
        println!("cargo:rerun-if-changed={}", file.display());
        patch_esp32_nimble(&file);
    } else {
        eprintln!("[build.rs] esp32-nimble source not found, skipping patch");
    }

    // Patch esp-idf-hal RMT encoder (bitmask fix)
    if let Some(file) = find_esp_idf_hal_encoder() {
        println!("cargo:rerun-if-changed={}", file.display());
        patch_rmt_encoder(&file);
    } else {
        eprintln!("[build.rs] esp-idf-hal encoder source not found, skipping patch");
    }
}

fn patch_esp32_nimble(file: &Path) {
    let src = std::fs::read_to_string(file).unwrap_or_default();

    if src.contains("// ecotiter-patch: unexpected_cfgs") {
        return;
    }

    eprintln!(
        "[build.rs] Patching esp32-nimble {} for IDF v6...",
        file.display()
    );

    // Prepend allow(unexpected_cfgs) — rustc does not recognise "6" as valid cfg value
    let allow = "#![allow(unexpected_cfgs)] // ecotiter-patch: unexpected_cfgs\n";
    let patched = format!("{allow}{src}");
    let patched = patched
        .replace(
            "all(\n      esp_idf_version_major = \"5\",\n      esp_idf_version_minor = \"5\"),",
            "all(\n      esp_idf_version_major = \"5\",\n      esp_idf_version_minor = \"5\"),\n    all(\n      esp_idf_version_major = \"6\"),",
        )
        .replace(
            "all(\n      esp_idf_version_major = \"5\",\n      esp_idf_version_minor = \"5\",\n    )",
            "all(\n      esp_idf_version_major = \"5\",\n      esp_idf_version_minor = \"5\",\n    ),\n    all(\n      esp_idf_version_major = \"6\",\n    )",
        );

    if src != patched {
        std::fs::write(file, &patched).expect("Failed to write patched ble_characteristic.rs");
        eprintln!("[build.rs] nimble patch applied OK");
    } else {
        eprintln!("[build.rs] nimble patch pattern not matched — file may need manual update");
    }
}

fn patch_rmt_encoder(file: &Path) {
    let src = std::fs::read_to_string(file).unwrap_or_default();

    if src.contains("// ecotiter-patch: rmt_encoder_bitmask") {
        return;
    }

    // Original upstream: match with exact values (breaks for bitmask combinations)
    const OLD: &str = r#"impl From<rmt_encode_state_t> for EncoderState {
    fn from(value: rmt_encode_state_t) -> Self {
        #[allow(non_upper_case_globals)]
        match value {
            rmt_encode_state_t_RMT_ENCODING_RESET => Self::EncodingReset,
            rmt_encode_state_t_RMT_ENCODING_COMPLETE => Self::EncodingComplete,
            rmt_encode_state_t_RMT_ENCODING_MEM_FULL => Self::EncodingMemoryFull,
            #[cfg(esp_idf_version_at_least_5_5_0)]
            rmt_encode_state_t_RMT_ENCODING_WITH_EOF => Self::EncodingWithEof,
            _ => panic!("Unknown rmt_encode_state_t value: {value}"),
        }
    }
}"#;

    // Fixed code: bitwise checks + marker
    const NEW: &str = r#"impl From<rmt_encode_state_t> for EncoderState {
    #[allow(non_upper_case_globals)]
    fn from(value: rmt_encode_state_t) -> Self {
        // ecotiter-patch: rmt_encoder_bitmask — use bitwise checks for bitmask type
        #[cfg(esp_idf_version_at_least_5_5_0)]
        if value & rmt_encode_state_t_RMT_ENCODING_WITH_EOF != 0 {
            return Self::EncodingWithEof;
        }
        if value & rmt_encode_state_t_RMT_ENCODING_MEM_FULL != 0 {
            return Self::EncodingMemoryFull;
        }
        if value & rmt_encode_state_t_RMT_ENCODING_COMPLETE != 0 {
            return Self::EncodingComplete;
        }
        Self::EncodingReset
    }
}"#;

    if src.contains(OLD) {
        let patched = src.replace(OLD, NEW);
        std::fs::write(file, &patched).expect("Failed to write patched encoder.rs");
        eprintln!("[build.rs] esp-idf-hal RMT encoder patch applied");
    } else if src.contains("RMT_ENCODING_WITH_EOF != 0") {
        // Already patched (by debugger) but without marker — insert it
        let header = "    fn from(value: rmt_encode_state_t) -> Self {\n";
        let marker =
            "        // ecotiter-patch: rmt_encoder_bitmask — use bitwise checks for bitmask type\n";
        if src.contains(header) {
            let patched = src.replace(header, &format!("{header}{marker}"));
            std::fs::write(file, &patched).expect("Failed to write marker to encoder.rs");
            eprintln!("[build.rs] esp-idf-hal RMT encoder marker added (debugger-patched)");
        } else {
            eprintln!("[build.rs] esp-idf-hal RMT encoder: unexpected function header format");
        }
    } else {
        eprintln!("[build.rs] esp-idf-hal RMT encoder: no recognizable pattern found");
    }
}

fn find_esp32_nimble_source() -> Option<PathBuf> {
    let cargo_home = std::env::var("CARGO_HOME").unwrap_or_else(|_| {
        let home = std::env::var("HOME")
            .or_else(|_| std::env::var("USERPROFILE"))
            .unwrap_or_else(|_| ".".into());
        format!("{home}/.cargo")
    });

    // Check git checkouts first (git dependency)
    let checkouts = PathBuf::from(&cargo_home).join("git/checkouts");
    if checkouts.exists() {
        if let Ok(entries) = std::fs::read_dir(&checkouts) {
            for entry in entries.flatten() {
                let dir_name = entry.file_name().to_string_lossy().to_string();
                if dir_name.starts_with("esp32-nimble") {
                    if let Ok(revs) = std::fs::read_dir(entry.path()) {
                        for rev in revs.flatten() {
                            let candidate = rev.path().join("src/server/ble_characteristic.rs");
                            if candidate.exists() {
                                return Some(candidate);
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback: local path dependency via Cargo.toml
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").ok()?;
    let cargo_toml = PathBuf::from(&manifest_dir).join("Cargo.toml");
    let content = std::fs::read_to_string(&cargo_toml).ok()?;

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("esp32-nimble") && trimmed.contains("path =") {
            if let Some(start) = trimmed.find("path = \"") {
                let rest = &trimmed[start + 8..];
                if let Some(end) = rest.find('"') {
                    let path_str = &rest[..end];
                    let candidate =
                        PathBuf::from(path_str).join("src/server/ble_characteristic.rs");
                    if candidate.exists() {
                        return Some(candidate);
                    }
                }
            }
        }
    }

    None
}

fn find_esp_idf_hal_encoder() -> Option<PathBuf> {
    let cargo_home = std::env::var("CARGO_HOME").unwrap_or_else(|_| {
        let home = std::env::var("HOME")
            .or_else(|_| std::env::var("USERPROFILE"))
            .unwrap_or_else(|_| ".".into());
        format!("{home}/.cargo")
    });

    // Check git checkouts first (git dependency)
    let checkouts = PathBuf::from(&cargo_home).join("git/checkouts");
    if checkouts.exists() {
        if let Ok(entries) = std::fs::read_dir(&checkouts) {
            for entry in entries.flatten() {
                let dir_name = entry.file_name().to_string_lossy().to_string();
                if dir_name.starts_with("esp-idf-hal") {
                    if let Ok(revs) = std::fs::read_dir(entry.path()) {
                        for rev in revs.flatten() {
                            let candidate = rev.path().join("src/rmt/encoder.rs");
                            if candidate.exists() {
                                return Some(candidate);
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback: local path dependency via Cargo.toml
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").ok()?;
    let cargo_toml = PathBuf::from(&manifest_dir).join("Cargo.toml");
    let content = std::fs::read_to_string(&cargo_toml).ok()?;

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("esp-idf-hal") && trimmed.contains("path =") {
            if let Some(start) = trimmed.find("path = \"") {
                let rest = &trimmed[start + 8..];
                if let Some(end) = rest.find('"') {
                    let path_str = &rest[..end];
                    let candidate = PathBuf::from(path_str).join("src/rmt/encoder.rs");
                    if candidate.exists() {
                        return Some(candidate);
                    }
                }
            }
        }
    }

    None
}
