//! ESP-IDF-safe mutex using the correct `PTHREAD_MUTEX_INITIALIZER` (`0xFFFFFFFF`).
//!
//! `std::sync::Mutex::new()` on ESP-IDF uses `mem::zeroed()` to initialize
//! the internal `pthread_mutex_t`, producing `0x00000000`. However, ESP-IDF v6
//! expects `PTHREAD_MUTEX_INITIALIZER = 0xFFFFFFFF` as a sentinel for lazy
//! initialization on first `pthread_mutex_lock()`. A zero-initialized mutex
//! causes `pthread_mutex_lock()` to dereference a NULL pointer, corrupting
//! heap (TLSF) metadata.
//!
//! This module mirrors `esp_idf_svc::private::Mutex` (which is not publicly
//! accessible) and correctly stores `0xFFFFFFFF` as the initial handle value.

#![allow(clippy::result_unit_err)]

use core::cell::UnsafeCell;

/// ESP-IDF `PTHREAD_MUTEX_INITIALIZER` — sentinel value `0xFFFFFFFF` that
/// triggers lazy `xSemaphoreCreateMutex()` allocation on first lock.
const PTHREAD_MUTEX_INIT: u32 = 0xFFFF_FFFF;

/// A mutex backed by ESP-IDF's POSIX `pthread_mutex_t` with correct
/// initialization.
///
/// Unlike `std::sync::Mutex`, this type stores `0xFFFFFFFF` (the ESP-IDF
/// sentinel) instead of all-zeros, ensuring the first `lock()` triggers
/// lazy initialization rather than dereferencing NULL.
pub struct EspMutex<T> {
    handle: UnsafeCell<u32>,
    data: UnsafeCell<T>,
}

// SAFETY: `pthread_mutex_lock` / `pthread_mutex_unlock` provide mutual exclusion.
unsafe impl<T: Send> Sync for EspMutex<T> {}
// SAFETY: `T: Send` ensures the contained data can be accessed from other threads.
unsafe impl<T: Send> Send for EspMutex<T> {}

impl<T> EspMutex<T> {
    /// Create a new mutex. The `pthread_mutex_t` handle is initialized to
    /// `0xFFFFFFFF` (ESP-IDF's lazy-init sentinel).
    pub const fn new(data: T) -> Self {
        Self {
            handle: UnsafeCell::new(PTHREAD_MUTEX_INIT),
            data: UnsafeCell::new(data),
        }
    }

    /// Lock the mutex, returning a guard that provides `&T` / `&mut T` access.
    ///
    /// On the first call, `pthread_mutex_lock()` detects the sentinel and
    /// triggers `xSemaphoreCreateMutex()` (heap allocation).
    pub fn lock(&self) -> Result<EspMutexGuard<'_, T>, ()> {
        // SAFETY: `handle` is a valid `pthread_mutex_t` initialized to the
        // ESP-IDF sentinel `0xFFFFFFFF`. The first lock triggers lazy init.
        let r = unsafe { esp_idf_sys::pthread_mutex_lock(self.handle.get().cast()) };
        if r == 0 {
            Ok(EspMutexGuard { mutex: self })
        } else {
            Err(())
        }
    }

    /// Non-blocking lock attempt. Returns `Err(())` if the mutex is already held.
    pub fn try_lock(&self) -> Result<EspMutexGuard<'_, T>, ()> {
        // SAFETY: Same as `lock()` — valid sentinel-initialized handle.
        let r = unsafe { esp_idf_sys::pthread_mutex_trylock(self.handle.get().cast()) };
        if r == 0 {
            Ok(EspMutexGuard { mutex: self })
        } else {
            Err(())
        }
    }
}

/// RAII guard returned by [`EspMutex::lock`] / [`EspMutex::try_lock`].
pub struct EspMutexGuard<'a, T> {
    mutex: &'a EspMutex<T>,
}

impl<T> Drop for EspMutexGuard<'_, T> {
    fn drop(&mut self) {
        // SAFETY: We hold the lock, so the mutex is in a valid locked state
        // and `pthread_mutex_unlock` is safe to call.
        unsafe {
            esp_idf_sys::pthread_mutex_unlock(self.mutex.handle.get().cast());
        }
    }
}

impl<T> core::ops::Deref for EspMutexGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        // SAFETY: We hold the lock, guaranteeing exclusive access to the data.
        unsafe { &*self.mutex.data.get() }
    }
}

impl<T> core::ops::DerefMut for EspMutexGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: We hold the lock, guaranteeing exclusive mutable access.
        unsafe { &mut *self.mutex.data.get() }
    }
}
