//! Safe wrappers around ESP-IDF boot-time and utility FFI calls.
//!
//! Each function encapsulates a single `unsafe { }` block with documented
//! safety invariants, exposing a safe Rust API.
//!
//! This module is only available on xtensa (ESP32) targets because it
//! depends on `esp_idf_sys`.

use core::num::NonZeroI32;

use esp_idf_sys;
use esp_idf_sys::EspError;

use crate::diag;

/// Disable the hardware watchdog timer (TWDT).
///
/// Must be called once at boot, before any FreeRTOS task uses the watchdog.
/// After this call, the hardware watchdog will not trigger a reset regardless
/// of task execution time.
///
/// # Safety (encapsulated)
///
/// `esp_task_wdt_deinit()` is safe to call from the main task at boot
/// (FreeRTOS scheduler is running). No dependencies on other tasks.
pub fn disable_wdt() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_WDT);
    // SAFETY:
    //   Invariant: esp_task_wdt_deinit requires FreeRTOS scheduler running.
    //   Context: called once at boot from main task.
    //   Risk: safe even if called multiple times (idempotent).
    unsafe {
        esp_idf_sys::esp_task_wdt_deinit();
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_WDT, 0);
}

/// Suppress debug-level logs from the ESP-IDF HTTP server txrx component.
///
/// Sets the log level for `httpd_txrx` to `ERROR` to reduce serial noise.
/// Safe to call at any point after `esp_idf_sys::link_patches()`.
pub fn suppress_httpd_txrx_logs() {
    // SAFETY:
    //   Invariant: c"httpd_txrx" is a valid null-terminated C string literal.
    //   esp_log_level_set modifies a global int only, no memory safety effects.
    //   Risk: wrong log level string = log spam, no UB.
    unsafe {
        esp_idf_sys::esp_log_level_set(
            c"httpd_txrx".as_ptr(),
            esp_idf_sys::esp_log_level_t_ESP_LOG_ERROR,
        );
    }
}

/// Check integrity of all heap regions using `heap_caps_check_integrity_all`.
///
/// Prints errors if corruption is found.
pub fn check_heap_integrity() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_HEAP);
    // SAFETY:
    //   Invariant: heap_caps_check_integrity_all is a read-only diagnostic call.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: none — read-only traversal, no side effects.
    let ok = unsafe { esp_idf_sys::heap_caps_check_integrity_all(true) };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_HEAP, 0);
    if ok {
        log::info!("Heap integrity OK");
    } else {
        log::error!("HEAP CORRUPTION DETECTED!");
    }
}

/// Read boot-time heap statistics.
///
/// Returns `(free_heap_bytes, largest_free_default_bytes, largest_free_dma_bytes)`.
///
/// All values come from read-only hardware registers via the ESP-IDF
/// heap allocator. Safe to call after FreeRTOS scheduler init.
pub fn heap_stats() -> (u32, u32, u32) {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_HEAP);
    // SAFETY:
    //   Invariant: All three FFI calls are read-only and access heap
    //   metadata only. No side effects on memory.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: stale values if called while heap is in use (always true).
    let result = unsafe {
        let free = esp_idf_sys::esp_get_free_heap_size();
        let largest_default =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DEFAULT);
        let largest_dma =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DMA);
        (
            free,
            u32::try_from(largest_default).unwrap_or(0),
            u32::try_from(largest_dma).unwrap_or(0),
        )
    };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_HEAP, 0);
    result
}

/// Read the current task's stack watermark (minimum free stack bytes
/// since task creation).
///
/// A return value below 1024 indicates critical risk of stack overflow.
/// Safe to call from any FreeRTOS task after scheduler init.
pub fn stack_watermark() -> u32 {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_WATERMARK);
    // SAFETY:
    //   Invariant: uxTaskGetStackHighWaterMark(NULL) queries the calling
    //   task's TCB (read-only field). Valid in any FreeRTOS task context.
    //   Context: safe after FreeRTOS scheduler init (main task).
    //   Risk: none — read-only TCB field access, idempotent.
    let result = unsafe { esp_idf_sys::uxTaskGetStackHighWaterMark(core::ptr::null_mut()) };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_WATERMARK, 0);
    result
}

/// Trigger a full ESP32 software restart.
///
/// Saves state to NVS before calling. This function does not return.
pub fn restart() -> ! {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_RESTART);
    // SAFETY:
    //   Invariant: esp_restart resets the CPU immediately. All state must be
    //   persisted before calling. Safe to call from any task context.
    //   Risk: function does not return. UB if called without saving state.
    unsafe {
        esp_idf_sys::esp_restart();
    }
}

/// Write a string directly to UART (stdout) without allocation.
///
/// Safe to call from any context including panic handler. Uses the raw
/// `write(1, ...)` syscall — no heap, no locks, no formatting.
/// Only for diagnostic emergency output.
pub fn panic_write_str(s: &str) {
    // SAFETY:
    //   Invariant: `write(1, ptr, len)` writes to the pre-opened stdout
    //   fd which is connected to UART on ESP-IDF. The write is async-signal-safe
    //   and does not allocate. Safe from any context including panic handler.
    //   Risk: partial write (returns < len) is silently ignored — OK for diagnostics.
    unsafe {
        esp_idf_sys::write(1, s.as_ptr().cast::<core::ffi::c_void>(), s.len());
    }
}

/// Install UART0 driver and connect VFS stdin to it.
///
/// Required for `std::io::stdin().read()` and `libc::read(0, ...)` to work.
/// Call once at boot before spawning any thread that reads from stdin.
///
/// ESP-IDF's boot console sets up stdout for polled register-based writes
/// (which is why `println!` works), but does NOT install the UART driver
/// required for blocking interrupt-driven reads. Without this call,
/// `read(0, ...)` fails because the VFS layer has no driver backing stdin.
///
/// This function:
///   1. Registers the UART VFS device (idempotent — safe to call multiple times)
///   2. Installs the UART0 driver with a 256-byte RX buffer
///   3. Switches the VFS to driver mode for blocking interrupt-driven I/O
///
/// # Safety (encapsulated)
///
/// `uart_vfs_dev_register()` is idempotent and safe after FreeRTOS scheduler
/// init. `uart_driver_install()` allocates DRAM for the RX buffer; the
/// amount (256 bytes) is negligible. `uart_vfs_dev_use_driver()` switches
/// the internal VFS dispatch table for UART0 — safe after driver install.
pub fn uart_init_stdin() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_UART_INIT);
    // SAFETY:
    //   Invariant: All three FFI calls are safe after FreeRTOS scheduler init.
    //   uart_vfs_dev_register: idempotent, creates VFS entries for /dev/uart/0.
    //   uart_driver_install: allocates RX buffer from DRAM, configures UART HW.
    //   uart_vfs_dev_use_driver: switches VFS from polled to driver mode.
    //   Context: called once at boot from main task, before any stdin reads.
    //   Risk: double-init handled by uart_is_driver_installed check.
    //         Allocation failure (256 bytes) is rare and logged, not fatal.
    unsafe {
        // Step 1: Register UART VFS driver (idempotent)
        esp_idf_sys::uart_vfs_dev_register();

        // Step 2: Install UART0 driver if not already installed
        if !esp_idf_sys::uart_is_driver_installed(esp_idf_sys::uart_port_t_UART_NUM_0) {
            let ret = esp_idf_sys::uart_driver_install(
                esp_idf_sys::uart_port_t_UART_NUM_0, // uart_num: UART0
                256,                                   // rx_buffer_size
                0,                                     // tx_buffer_size (0 = no TX buffering)
                10,                                    // queue_size (RX event queue depth)
                core::ptr::null_mut(),                 // uart_queue (NULL = don't need handle)
                0,                                     // intr_alloc_flags (default)
            );
            if ret != 0 {
                // Non-fatal: stdin simply won't work, but we log it
                log::error!("UART: uart_driver_install failed: {ret:#x}");
            }
        }

        // Step 3: Switch VFS to driver mode for blocking interrupt-driven I/O
        // Note: uart_vfs_dev_use_driver takes c_int (i32), not uart_port_t (c_uint).
        // UART_NUM_0 = 0, fits safely in i32.
        #[allow(clippy::cast_possible_wrap)]
        esp_idf_sys::uart_vfs_dev_use_driver(esp_idf_sys::uart_port_t_UART_NUM_0 as i32);
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_UART_INIT, 0);
}

/// Read bytes from UART0 stdin with blocking semantics.
///
/// Wraps `esp_idf_sys::uart_read_bytes()` with `portMAX_DELAY`, so this call
/// blocks until at least one byte is received on UART0. Returns the number of
/// bytes read (always > 0 for a successful call).
///
/// This is a direct FFI call that bypasses the VFS/std layer entirely,
/// avoiding the pthread/Rust std issues that caused `std::io::stdin().read()`
/// to panic on ESP-IDF v6 (LL-008 pattern: UART reader thread crash).
///
/// # Precondition
///
/// `uart_init_stdin()` MUST have been called once at boot to install the UART
/// driver. Otherwise `uart_read_bytes` will return -1 (error).
///
/// # Safety (encapsulated)
///
/// `uart_read_bytes` writes into a caller-owned buffer. The UART driver must
/// be installed (guaranteed by `uart_init_stdin()`). Safe from any task
/// context after driver install.
///
/// # Returns
///
/// - `Ok(n)` — `n` bytes were read into `buf` (1 ≤ n ≤ buf.len()).
/// - `Err(EspError)` — UART driver not installed or parameter error.
///
/// # Panics
///
/// Panics if `NonZeroI32::new()` fails, which cannot happen in practice
/// because `ret < 0` is verified before the conversion.
pub fn uart_read_stdin_blocking(buf: &mut [u8]) -> Result<usize, EspError> {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_UART_READ);
    let len = u32::try_from(buf.len()).unwrap_or(u32::MAX);
    // SAFETY:
    //   Invariant: uart_read_bytes writes to a caller-owned buffer, UART
    //   driver must be installed. This is guaranteed by uart_init_stdin()
    //   having been called at boot (see main.rs).
    //   Context: called from UART reader thread after uart_init_stdin().
    //   Risk: safe — buf is owned stack memory, UART_NUM_0 is valid,
    //         portMAX_DELAY = u32::MAX blocks until data arrives (no busy-wait).
    let ret = unsafe {
        esp_idf_sys::uart_read_bytes(
            esp_idf_sys::uart_port_t_UART_NUM_0,
            buf.as_mut_ptr().cast::<core::ffi::c_void>(),
            len,
            u32::MAX, // portMAX_DELAY — block until ≥1 byte received
        )
    };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_UART_READ, if ret < 0 { -1 } else { 0 });
    if ret < 0 {
        #[allow(clippy::expect_used)]
        let nz = NonZeroI32::new(ret).expect("ret < 0 => non-zero");
        Err(EspError::from_non_zero(nz))
    } else {
        Ok(ret.try_into().unwrap_or(0))
    }
}

/// Initialize the lwIP TCP/IP network interface stack.
///
/// Must be called once at boot, before any network operations (WiFi, HTTP,
/// BLE). `esp_netif_init()` is internally idempotent (guarded by a static
/// flag), so calling it before `WifiManager::new()` has no effect if WiFi init
/// succeeds, and ensures the lwIP thread exists if WiFi init fails.
///
/// # Safety (encapsulated)
///
/// `esp_netif_init()` creates the lwIP tcpip thread and its mbox. Safe to
/// call from any context after FreeRTOS scheduler init. Idempotent —
/// subsequent calls are no-ops.
pub fn netif_init() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NETIF_INIT);
    // SAFETY:
    //   Invariant: esp_netif_init creates lwIP tcpip thread + mbox.
    //   Context: called once at boot from net_owner thread after scheduler init.
    //   Risk: safe — idempotent (ESP-IDF internal guard).
    unsafe {
        esp_idf_sys::esp_netif_init();
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_NETIF_INIT, 0);
}

/// Set BT/WiFi coexistence priority to prefer BLE.
///
/// Safe to call once at init before any radio activity. Uses a simple
/// register write — no side effects on memory safety.
pub fn set_coex_ble_preferred() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_COEX);
    // SAFETY:
    //   Invariant: esp_coex_preference_set is a register write, no memory effects.
    //   Context: called once at init before any radio activity.
    //   Risk: if called later, may cause brief radio renegotiation; no UB.
    unsafe {
        esp_idf_sys::esp_coex_preference_set(esp_idf_sys::esp_coex_prefer_t_ESP_COEX_PREFER_BT);
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_COEX, 0);
}

/// Retry esp_wifi_deinit() up to `max_attempts` times with delay.
///
/// ESP-IDF v6.0.1 has a race where esp_wifi_init() partially fails (task
/// creation fails) and then esp_wifi_deinit() also fails with 0x3001
/// because the internal WiFi task handle is NULL, leaving buffers leaked.
/// Retrying with delay allows deferred cleanup to complete.
///
/// Returns true if deinit succeeded within attempts.
pub fn wifi_deinit_retry(max_attempts: u32) -> bool {
    for attempt in 1..=max_attempts {
        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_WIFI_DEINIT);
        // SAFETY: esp_wifi_deinit is safe if called after esp_wifi_init
        // (even failed init). It may return error code but no UB.
        let ret = unsafe { esp_idf_sys::esp_wifi_deinit() };
        diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_WIFI_DEINIT, if ret == 0 { 0 } else { -1 });
        if ret == 0 {
            return true;
        }
        log::warn!("WiFi: esp_wifi_deinit attempt {attempt}/{max_attempts} failed ({ret:#x})");
        // SAFETY: esp_rom_delay_us is a busy-wait microsecond delay safe
        // from any context. No heap or OS calls.
        unsafe { esp_idf_sys::esp_rom_delay_us(10_000) }; // 10ms
    }
    false
}

/// Read the ESP32 hardware microsecond timer.
///
/// Returns a 64-bit microsecond timestamp from the hardware timer.
/// Safe to call from any context (read-only HW register).
/// Used for diagnostic main loop timing.
pub fn micros() -> u64 {
    // SAFETY: esp_timer_get_time() is a read-only hardware register read.
    // Safe from any context — no heap, no locks, no OS calls.
    #[allow(clippy::cast_sign_loss)]
    unsafe {
        esp_idf_sys::esp_timer_get_time() as u64
    }
}

// ── Hardware exception panic handler (linker-wrapped) ────────────────
//
// These structures match ESP-IDF v6.0.1 C layout for the panic handler.
// Used via linker `--wrap` to intercept ALL crash types (not just Rust panics).

/// Matches `panic_info_t` from ESP-IDF
/// `components/esp_system/include/esp_private/panic_internal.h`
#[repr(C)]
#[allow(dead_code)]
struct PanicInfo {
    core: i32,
    exception: u32,
    reason: *const u8,
    description: *const u8,
    details: Option<unsafe extern "C" fn(*const u8)>,
    state: Option<unsafe extern "C" fn(*const u8)>,
    addr: *const u8,
    frame: *const u8,
    pseudo_excause: bool,
}

/// Matches `XtExcFrame` from ESP-IDF
/// `components/xtensa/include/xtensa_context.h`
/// ESP32 windowed ABI with hardware loops (XCHAL_HAVE_LOOPS=1).
#[repr(C)]
#[allow(dead_code)]
struct XtExcFrame {
    exit: i32,     // offset 0
    pc: i32,       // offset 4
    ps: i32,       // offset 8
    a0: i32,       // offset 12
    a1: i32,       // offset 16
    a2: i32,       // offset 20
    a3: i32,       // offset 24
    a4: i32,       // offset 28
    a5: i32,       // offset 32
    a6: i32,       // offset 36
    a7: i32,       // offset 40
    a8: i32,       // offset 44
    a9: i32,       // offset 48
    a10: i32,      // offset 52
    a11: i32,      // offset 56
    a12: i32,      // offset 60
    a13: i32,      // offset 64
    a14: i32,      // offset 68
    a15: i32,      // offset 72
    sar: i32,      // offset 76
    exccause: i32, // offset 80
    excvaddr: i32, // offset 84
    lbeg: i32,     // offset 88
    lend: i32,     // offset 92
    lcount: i32,   // offset 96
    tmp0: i32,     // offset 100
    tmp1: i32,     // offset 104
    tmp2: i32,     // offset 108
}

// ── EXCCAUSE name table ──────────────────────────────────────────

/// Standard Xtensa exception cause names for EXCCAUSE values 0–39.
/// Maps exception cause numbers to human-readable names.
const EXCCAUSE_NAMES: [&str; 40] = [
    "IllegalInstruction",    // 0
    "Syscall",               // 1
    "InstructionFetchError", // 2
    "LoadStoreError",        // 3
    "Level1Interrupt",       // 4
    "Alloca",                // 5
    "IntegerDivideByZero",   // 6
    "PCValue",               // 7
    "Privileged",            // 8
    "LoadStoreAlignment",    // 9
    "Reserved10",            // 10
    "Reserved11",            // 11
    "InstrPDAddrError",      // 12
    "LoadStorePIFDataError", // 13
    "InstrPIFAddrError",     // 14
    "LoadStorePIFAddrError", // 15
    "InstTLBMiss",           // 16
    "InstTLBMultiHit",       // 17
    "InstFetchPrivilege",    // 18
    "Reserved19",            // 19
    "InstFetchProhibited",   // 20
    "Reserved21",            // 21
    "Reserved22",            // 22
    "Reserved23",            // 23
    "LoadStoreTLBMiss",      // 24
    "LoadStoreTLBMultihit",  // 25
    "LoadStorePrivilege",    // 26
    "Reserved27",            // 27
    "LoadProhibited",        // 28
    "StoreProhibited",       // 29
    "Reserved30",            // 30
    "Reserved31",            // 31
    "Cp0Dis",                // 32
    "Cp1Dis",                // 33
    "Cp2Dis",                // 34
    "Cp3Dis",                // 35
    "Cp4Dis",                // 36
    "Cp5Dis",                // 37
    "Cp6Dis",                // 38
    "Cp7Dis",                // 39
];

/// Returns the human-readable name for an Xtensa EXCCAUSE value (0–39).
/// Returns "Unknown" for values outside the standard range.
const fn exccause_name(exccause: u32) -> &'static str {
    if (exccause as usize) < EXCCAUSE_NAMES.len() {
        EXCCAUSE_NAMES[exccause as usize]
    } else {
        "Unknown"
    }
}

// ── Backtrace helpers (Xtensa windowed ABI) ──────────────────────

/// Adjust a return address to point at the call instruction that
/// produced it.  Windowed calls store A0 with the high bit set;
/// this normalises the address and subtracts 3 bytes (call
/// instruction length in Xtensa).
const fn process_pc(pc: u32) -> u32 {
    let pc = if pc & 0x8000_0000 != 0 {
        (pc & 0x3FFF_FFFF) | 0x4000_0000
    } else {
        pc
    };
    pc.wrapping_sub(3)
}

/// Check whether a stack-pointer value falls inside the ESP32 DRAM
/// region where stack memory lives.
fn is_sane_sp(sp: u32) -> bool {
    (0x3FFB_0000..0x4000_0000).contains(&sp)
}

/// Check whether an address lies in executable memory (IRAM or flash
/// cache).
fn is_executable(pc: u32) -> bool {
    // IRAM range
    (0x4000_0000..0x400C_2000).contains(&pc)
        // Flash cache range
        || (0x400D_0000..0x4040_0000).contains(&pc)
}

/// Print a single backtrace entry in machine-parseable format:
/// `0xPC:0xSP`
fn print_bt_entry(w: &mut impl core::fmt::Write, pc: u32, sp: u32) {
    let _ = writeln!(w, "0x{pc:08x}:0x{sp:08x}");
}

/// Walk the Xtensa windowed-ABI call stack starting from the
/// exception frame and print each frame.
///
/// The algorithm reads the base-save area at [sp-16] (return
/// address) and [sp-12] (previous frame sp), which the `entry`
/// instruction / window-underflow handler has left behind.
#[allow(clippy::cast_sign_loss)]
fn do_backtrace(w: &mut impl core::fmt::Write, frame: *const XtExcFrame) {
    if frame.is_null() {
        return;
    }
    // SAFETY: caller guarantees frame points to a valid XtExcFrame
    // pushed by CPU exception handling.
    let f = unsafe { &*frame };
    let mut pc = f.pc as u32;
    let mut sp = f.a1 as u32;
    let mut next_pc = f.a0 as u32;
    let mut corrupted = false;

    // First frame = the crash site
    print_bt_entry(w, process_pc(pc), sp);

    for _ in 0..100 {
        if next_pc == 0 {
            break;
        }
        if !is_sane_sp(sp) {
            corrupted = true;
            break;
        }

        // Read the base save area pushed by `entry` or the window
        // underflow handler: [sp-16] = saved A0 (return address),
        // [sp-12] = saved A1 (caller's stack pointer).
        // SAFETY: sp has been checked against DRAM range via
        // is_sane_sp() above.  In panic context exceptions are
        // masked (PS.EXCM=1), so a bad read returns whatever is on
        // the bus rather than triggering a double fault.
        unsafe {
            let base_save = sp;
            pc = next_pc;
            next_pc = core::ptr::read_volatile((base_save - 16) as *const u32);
            sp = core::ptr::read_volatile((base_save - 12) as *const u32);
        }

        let adj_pc = process_pc(pc);

        if !is_executable(adj_pc) {
            corrupted = true;
            break;
        }
        if !is_sane_sp(sp) {
            corrupted = true;
            break;
        }

        print_bt_entry(w, adj_pc, sp);
    }

    if corrupted {
        let _ = writeln!(w, "|<-CORRUPTED");
    }
}

extern "C" {
    /// The real (original) `esp_panic_handler` function, made available
    /// by the linker's `--wrap` flag. Called at the end of our wrapper
    /// to perform the normal ESP-IDF panic processing (reset, coredump).
    /// Takes `void*` which is compatible with `panic_info_t*` (repr(C)).
    fn __real_esp_panic_handler(info: *const core::ffi::c_void);
}

/// ESP32 UART0 base address (AHB bus).
const UART0_BASE: u32 = 0x3FF4_0000;
/// UART FIFO AHB register — write a byte here to transmit on UART0.
const UART_FIFO_AHB: *mut u8 = UART0_BASE as *mut u8;
/// UART Status register — bits 16..23 hold `txfifo_cnt` (0–128).
const UART_STATUS: *mut u32 = (UART0_BASE + 0x1C) as *mut u32;
/// Maximum TX FIFO depth on ESP32 UART.
const UART_TX_FIFO_SIZE: u32 = 128;

/// Writer that outputs directly to UART0 via MMIO register writes.
///
/// Safe from any context including panic handler — no OS calls, no locks,
/// no heap allocation. This is the same approach ESP-IDF's own panic
/// handler uses for crash output.
struct CrashWriter;

impl core::fmt::Write for CrashWriter {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &byte in s.as_bytes() {
            // Wait for TX FIFO to have room (i.e., not completely full).
            // SAFETY: UART register MMIO is safe from any context —
            // no OS calls, no locks, no heap. In the worst case
            // (corrupted registers or dead UART), we spin forever,
            // which is acceptable best-effort diagnostic behaviour.
            unsafe {
                loop {
                    let status = core::ptr::read_volatile(UART_STATUS);
                    let txfifo_cnt = (status >> 16) & 0xFF;
                    if txfifo_cnt < UART_TX_FIFO_SIZE {
                        break;
                    }
                }
                core::ptr::write_volatile(UART_FIFO_AHB, byte);
            }
        }
        Ok(())
    }
}

/// Wrapped `esp_panic_handler` — intercepts ALL crash types (hardware
/// exceptions AND Rust panics) via linker `--wrap`, dumping a
/// machine-parseable crash dump optimised for AI agent analysis.
///
/// Output format (each section is predictable):
///
/// ```text
/// === CRASH ===
/// exccause=0 name=IllegalInstruction pc=0x40091100 excvaddr=0x0 ps=0x60730 sp=0x3fffcee0
/// === REGISTERS ===
/// a0=0x800d539a a1=0x3fffcee0 a2=0x0 a3=0x0
/// ...
/// sar=0x0 exccause=0x0 excvaddr=0x0 lbeg=0x0 lend=0x0 lcount=0x0
/// === BACKTRACE ===
/// 0x40091100:0x3fffcee0
/// 0x800d539a:0x3fffcec0 |<-CORRUPTED
/// ```
///
/// # Safety
///
/// Called from ESP-IDF panic context — IRAM may be corrupted, heap may be
/// inconsistent. This function uses only lock-free volatile reads and raw
/// UART writes. It must NOT allocate, lock mutexes, or use `format!()`.
#[no_mangle]
#[allow(clippy::not_unsafe_ptr_arg_deref, clippy::cast_sign_loss)]
pub unsafe extern "C" fn __wrap_esp_panic_handler(info: *const core::ffi::c_void) {
    use core::fmt::Write;

    let mut w = CrashWriter;

    if !info.is_null() {
        // SAFETY: checked above that info is non-null. The panic_info_t
        // pointer is guaranteed valid by ESP-IDF's panic dispatch.
        let pi = unsafe { &*(info.cast::<PanicInfo>()) };

        if !pi.frame.is_null() {
            // SAFETY: The frame pointer points to a valid XtExcFrame
            // pushed by the CPU exception handling.
            let frame: *const XtExcFrame = pi.frame.cast();
            // SAFETY: frame is non-null (checked) and points to a
            // valid XtExcFrame. All field reads are aligned u32 values.
            let f = unsafe { &*frame };

            let exccause = f.exccause as u32;
            let pc = f.pc as u32;
            let excvaddr = f.excvaddr as u32;
            let ps = f.ps as u32;
            let sp = f.a1 as u32;
            let name = exccause_name(exccause);

            let _ = writeln!(&mut w, "=== CRASH ===");
            let _ = writeln!(
                &mut w,
                "exccause={exccause} name={name} pc={pc:#010x} excvaddr={excvaddr:#010x} ps={ps:#010x} sp={sp:#010x}",
            );

            let _ = writeln!(&mut w, "=== REGISTERS ===");
            let _ = writeln!(
                &mut w,
                "a0={:#010x} a1={:#010x} a2={:#010x} a3={:#010x}",
                f.a0 as u32, f.a1 as u32, f.a2 as u32, f.a3 as u32,
            );
            let _ = writeln!(
                &mut w,
                "a4={:#010x} a5={:#010x} a6={:#010x} a7={:#010x}",
                f.a4 as u32, f.a5 as u32, f.a6 as u32, f.a7 as u32,
            );
            let _ = writeln!(
                &mut w,
                "a8={:#010x} a9={:#010x} a10={:#010x} a11={:#010x}",
                f.a8 as u32, f.a9 as u32, f.a10 as u32, f.a11 as u32,
            );
            let _ = writeln!(
                &mut w,
                "a12={:#010x} a13={:#010x} a14={:#010x} a15={:#010x}",
                f.a12 as u32, f.a13 as u32, f.a14 as u32, f.a15 as u32,
            );
            let _ = writeln!(
                &mut w,
                "sar={:#010x} exccause={:#010x} excvaddr={:#010x} lbeg={:#010x} lend={:#010x} lcount={:#010x}",
                f.sar as u32, f.exccause as u32, f.excvaddr as u32,
                f.lbeg as u32, f.lend as u32, f.lcount as u32,
            );

            // Backtrace
            let _ = writeln!(&mut w, "=== BACKTRACE ===");
            do_backtrace(&mut w, frame);
        }
    }

    // Dump diagnostic black box events
    diag::black_box::dump(&mut w);
    // Dump all registered thread stack watermarks
    diag::stack_monitor::emergency_dump(&mut w);

    // End-of-dump marker — used by extract_crash_section_from_log() to
    // locate the boundary between our diagnostic output and any secondary
    // crash (e.g. lwIP assert from __real_esp_panic_handler).
    let _ = writeln!(&mut w, "!!! EXCEPTION END !!!");

    // Call the real ESP-IDF panic handler (performs reset, coredump, etc.)
    // SAFETY: __real_esp_panic_handler is the original C panic handler.
    // It is safe to call from panic context as it only saves state and resets.
    unsafe {
        __real_esp_panic_handler(info);
    }
}
