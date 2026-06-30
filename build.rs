#![allow(
    clippy::disallowed_types,    // Vec<String> OK in build script (not hot path)
    clippy::if_not_else,         // build script logic fine as-is
)]

use embuild as _;
use embuild::build::LinkArgs;
use std::path::PathBuf;

fn main() {
    // Propagate linker arguments from esp-idf-sys
    // NOTE: lib_name must be UPPERCASE to match cargo's env var naming (DEP_{LIB_NAME}_EMBUILD_LINK_ARGS)
    LinkArgs::output_propagated("ESP_IDF").unwrap_or_else(|e| {
        println!("cargo:warning=LinkArgs::output_propagated failed: {e}");
    });

    // Suppress cfg warning for esp32-nimble IDF v6 patches (our crate only)
    println!("cargo::rustc-check-cfg=cfg(esp_idf_version_major, values(\"6\"))");

    // Only patch on xtensa targets
    let target = std::env::var("TARGET").unwrap_or_default();
    if !target.contains("xtensa") {
        return;
    }

    let Some(file) = find_esp32_nimble_source() else {
        eprintln!("[build.rs] esp32-nimble source not found, skipping patch");
        return;
    };

    println!("cargo:rerun-if-changed={}", file.display());

    let src = std::fs::read_to_string(&file).unwrap_or_default();

    // Check if all patches are already applied
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
        std::fs::write(&file, &patched).expect("Failed to write patched ble_characteristic.rs");
        eprintln!("[build.rs] Patch applied OK");
    } else {
        eprintln!("[build.rs] Patch pattern not matched — file may need manual update");
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
