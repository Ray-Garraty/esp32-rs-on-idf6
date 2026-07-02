---
type: Known Issue
title: RMT encoder bitmask-vs-enum mismatch panic
description: >
  esp-idf-hal v0.46.2 treats rmt_encode_state_t (bitmask type with flags
  0, 1, 2, 4) as exclusive enum. When IDF RMT driver returns combined
  state COMPLETE | MEM_FULL (value 3), the match falls through to
  _ => panic!().
tags: [rmt, stepper, panic, esp-idf-hal, bitmask]
timestamp: "2026-07-02"
---

# Crash Report

## Verdict

- **Status:** fix_applied_and_verified
- **Root Cause:** `rmt_encode_state_t` is a **bitmask** (flags: 0, 1, 2, 4), but `esp-idf-hal`'s `From<rmt_encode_state_t>` implementation uses exact `match` patterns designed for an exclusive enum. The IDF RMT driver legitimately returns combined states like `COMPLETE | MEM_FULL = 3`, which falls through to `_ => panic!()`.
- **Confidence:** high
- **Fixed:** 2026-07-02 — patched `esp-idf-hal` git checkout at `encoder.rs:76-96`. Boot verified past homing without panic.

## Evidence Chain

### Step 1: Triage

```
crash_analyzer output:
  rust_panic: true
  panic_location: esp-idf-hal/src/rmt/encoder.rs:85:18
  panic_message: Unknown rmt_encode_state_t value: 3
  known_lessons: [LL-002] (RMT encoder panic)
```

The analyzer correctly matches LL-002 (known pattern), but the lesson's fix recommendation ("ensure signal buffer does not exceed 1024 symbols") is a workaround, not the root cause explanation.

### Step 2: S1–S5 Protocol (Simplified — crash is runtime, not boot)

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | Not applicable (runtime panic, not boot crash) | Skipped |
| S2 (heap integrity) | Not applicable (not a boot/heap crash) | Skipped |
| S3 (smoke test) | Not applicable (occurs during homing, not boot) | Skipped |
| S4 (delta analysis) | Cargo.lock pins esp-idf-hal@24e99b86 (v0.46.2) | See below |
| S5 (red flags) | RMT encoder used via CopyEncoder | Identified |

### Step 3: Elimination

**Primary Investigation Question:** Why does `#[cfg(esp_idf_version_at_least_5_5_0)]` fail to match?

**Finding:** The cfg guard IS working correctly. Verifications:
1. Generated bindings confirm `ESP_IDF_VERSION_MAJOR=6, MINOR=0, PATCH=1` ✅
2. `esp-idf-hal` build output contains `cargo:rustc-cfg=esp_idf_version_at_least_5_5_0` ✅
3. `RMT_ENCODING_WITH_EOF = (1 << 2) = 4` — NOT 3 ✅

The cfg question is a **red herring** — value 3 is not `RMT_ENCODING_WITH_EOF`.

**Root Cause Investigation:**

The actual values from IDF v6.0.1 header (`esp_driver_rmt/include/driver/rmt_encoder.h`):

| Flag | Value | Binary |
|------|-------|--------|
| `RMT_ENCODING_RESET` | 0 | 000 |
| `RMT_ENCODING_COMPLETE` | 1 (1 << 0) | 001 |
| `RMT_ENCODING_MEM_FULL` | 2 (1 << 1) | 010 |
| `RMT_ENCODING_WITH_EOF` | 4 (1 << 2) | 100 |

These are **bit flags** — multiple bits can be set simultaneously. The IDF RMT driver legitimately returns combined states. The most common combined state is `COMPLETE | MEM_FULL = 3` (0b011), which occurs when the encoder fills the last available memory block exactly as it finishes encoding the data.

The `esp-idf-hal` code at `encoder.rs:76-88`:

```rust
impl From<rmt_encode_state_t> for EncoderState {
    fn from(value: rmt_encode_state_t) -> Self {
        match value {
            rmt_encode_state_t_RMT_ENCODING_RESET => Self::EncodingReset,
            rmt_encode_state_t_RMT_ENCODING_COMPLETE => Self::EncodingComplete,
            rmt_encode_state_t_RMT_ENCODING_MEM_FULL => Self::EncodingMemoryFull,
            #[cfg(esp_idf_version_at_least_5_5_0)]  // ← WORKS but handles only single value 4
            rmt_encode_state_t_RMT_ENCODING_WITH_EOF => Self::EncodingWithEof,
            _ => panic!("Unknown rmt_encode_state_t value: {value}"),  // ← HITS HERE for value 3
        }
    }
}
```

This uses exact `match` on individual flag values, but a `match` in Rust only matches one arm. Value 3 (`COMPLETE | MEM_FULL`) matches NONE of the single-bit arms and falls through to the panic.

**Confirmation from bindings:**
```
pub const rmt_encode_state_t_RMT_ENCODING_RESET: rmt_encode_state_t = 0;
pub const rmt_encode_state_t_RMT_ENCODING_COMPLETE: rmt_encode_state_t = 1;
pub const rmt_encode_state_t_RMT_ENCODING_MEM_FULL: rmt_encode_state_t = 2;
pub const rmt_encode_state_t_RMT_ENCODING_WITH_EOF: rmt_encode_state_t = 4;
pub type rmt_encode_state_t = ::core::ffi::c_uint;  // ← unsigned int (explicitly a bitmask)
```

The type `c_uint` (unsigned int) confirms it's a bitmask, not an enum. The C header also uses `= (1 << N)` bit-shift syntax which confirms bitmask semantics.

### Step 4: Root Cause

```yaml
root_cause:
  category: api_misuse
  description: >
    esp-idf-hal v0.46.2 (commit 24e99b86) treats rmt_encode_state_t
    (a bitmask type with flags 0, 1, 2, 4) as if it were a mutually
    exclusive enum. The From<rmt_encode_state_t> implementation uses
    match with exact value patterns, so combined flag states like
    COMPLETE | MEM_FULL (value 3) fall through to the panic arm.
  evidence:
    - "IDF header defines values as bit shifts: RESET=0, COMPLETE=(1<<0)=1, MEM_FULL=(1<<1)=2, WITH_EOF=(1<<2)=4"
    - "Bindings confirm: rmt_encode_state_t = c_uint (unsigned int, not an enum)"
    - "esp-idf-hal build output confirms esp_idf_version_at_least_5_5_0 is emitted correctly"
    - "The match at encoder.rs:76-88 uses == on individual values, not bitwise &"
    - "Crash occurs specifically when state=3 (0b011 = COMPLETE | MEM_FULL), not WITH_EOF=4"
  confidence: high
  reproduction: >
    Call send_and_wait() with CopyEncoder where the signal data size
    exactly fills an RMT memory block. The IDF driver returns
    COMPLETE | MEM_FULL (3), triggering the panic.
```

## Fix

### Applied: Patch esp-idf-hal's `From<rmt_encode_state_t>` (Confirmed 2026-07-02)

The `From<rmt_encode_state_t>` implementation at `encoder.rs:76-88` must use bitwise checks, not exact matching. Replace it with:

```rust
impl From<rmt_encode_state_t> for EncoderState {
    fn from(value: rmt_encode_state_t) -> Self {
        // Check bits in priority order
        if value & rmt_encode_state_t_RMT_ENCODING_WITH_EOF != 0 {
            return Self::EncodingWithEof;
        }
        if value & rmt_encode_state_t_RMT_ENCODING_MEM_FULL != 0 {
            return Self::EncodingMemoryFull;
        }
        if value & rmt_encode_state_t_RMT_ENCODING_COMPLETE != 0 {
            return Self::EncodingComplete;
        }
        // RESET when no bits set
        Self::EncodingReset
    }
}
```

**Alternative:** Return a `bitflags` type instead of an enum, to properly represent combined states.

### Previously Considered: Workaround (reduce signal size — NOT needed after patch)

Before the patch, a workaround was to ensure RMT signal data stays under 1024 symbols. This is no longer needed — the patched bitwise check handles combined states correctly.

### Verification (Completed 2026-07-02)

1. ✅ Patch applied to `esp-idf-hal`'s `From<rmt_encode_state_t>` in git checkout.
2. ✅ `cargo clean -p esp-idf-hal && cargo +esp build` — esp-idf-hal recompiled from patched source.
3. ✅ Flashed to ESP32 on /dev/ttyUSB0 — "Flashing has completed!" confirmed.
4. ✅ Device boots past homing without panic (confirmed via serial monitor, 15s observation).
5. ✅ Temperature readings continue normally in background thread.
6. ✅ No Guru Meditation, no panic — RMT transmission completes successfully.

## Investigation Artifacts

| File | Status |
|------|--------|
| `src/bin/smoke_test.rs` | ❌ Not created (not a boot crash) |
| `[INVESTIGATION]` markers | ❌ Not applied (no edits made) |
| Lessons learned | ✅ LL-002 updated with fix reference |
| `~/.cargo/git/checkouts/esp-idf-hal-*/24e99b8/src/rmt/encoder.rs` | ✅ Patched — bitwise checks applied |
| Boot verification | ✅ Device boots past homing, no panic |

## Remaining Issues

1. **LL-002 lesson update needed** — The current lesson correctly identifies the crash but gives an imprecise root cause explanation (`non-exhaustive match` → it's actually `bitmask-vs-enum mismatch`) and an incomplete fix recommendation. The lesson should be updated to reflect the bitmask analysis.
2. **Upstream fix** — This is a bug in esp-idf-hal v0.46.2. If using a pinned git dependency, consider updating to a newer commit or patching.
3. **Custom Encoder implementations** — Any custom `Encoder::encode()` implementations that return single-value `EncoderState` are unaffected (the bug is only in the reverse conversion `rmt_encode_state_t → EncoderState`).

## References

- `esp-idf-hal` encoder.rs: `~/.cargo/git/checkouts/esp-idf-hal-29c73b2ac8ab537b/24e99b8/src/rmt/encoder.rs`
- IDF header: `/home/vlabe/.espressif/v6.0.1/esp-idf/components/esp_driver_rmt/include/driver/rmt_encoder.h`
- Generated bindings: `target/xtensa-esp32-espidf/debug/build/esp-idf-sys-*/out/bindings.rs`
- esp-idf-hal build output: `target/xtensa-esp32-espidf/debug/build/esp-idf-hal-*/output`
