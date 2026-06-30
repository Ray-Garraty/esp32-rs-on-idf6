//! BLE NUS (Nordic UART Service) manager.
//!
//! # Architecture
//!
//! ```text
//! BLE NOTIFY THREAD (std::thread, 8KB stack):
//!   loop {
//!     recv on status_rx (mpsc blocking)
//!     if connected { notify(tx_char, serialized_json) }
//!     else { drop message }
//!   }
//!
//! BLE RX CALLBACK (NimBLE internal thread):
//!   on write to RX characteristic {
//!     parse bytes as CommandEnvelope
//!     cmd_tx.try_send(envelope) — bounded via sync_channel
//!     // On SendError (full channel): drop silently (backpressure)
//!   }
//!
//! MAIN LOOP:
//!   cmd_rx.try_recv() → process command → dispatch → response
//!   ble_mgr.process() → zombie defense level 2
//! ```
//!
//! # Zombie Defense (3 levels)
//!
//! - Level 1: In `ble_send()` — 5 consecutive notify failures → `zombie_kill()`
//! - Level 2: In `process()` — if `connected_count()==0` but `G_BLE_CONNECTED==true`
//! - Level 3: In `ble_send()` — if `getConnectedCount()==0` but local flag→true

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc};

use esp_idf_sys::{esp_coex_prefer_t_ESP_COEX_PREFER_BT, esp_coex_preference_set};

use esp32_nimble::enums::{PowerLevel, PowerType};
use esp32_nimble::utilities::BleUuid;
use esp32_nimble::{BLEAdvertisementData, BLECharacteristic, BLEDevice, NimbleProperties};

use crate::application::command::CommandEnvelope;
use crate::config;
use crate::domain::channels::StatusUpdate;

use crate::errors::NetworkError;

/// Global BLE connection flag for zombie defense.
///
/// Set by on_subscribe callback, read by main loop and notify thread.
pub static G_BLE_CONNECTED: AtomicBool = AtomicBool::new(false);

/// BLE NUS manager.
pub struct BleManager {
    /// TX characteristic (notify) — stored for sending notifications.
    /// Uses esp32-nimble's Mutex (not std::sync::Mutex).
    tx_char: Option<Arc<esp32_nimble::utilities::mutex::Mutex<BLECharacteristic>>>,
    /// Shared connection flag.
    connected: Arc<AtomicBool>,
    /// Sender for commands received via BLE RX.
    cmd_tx: mpsc::SyncSender<CommandEnvelope>,
    /// Consecutive notify failures (zombie defense level 1 counter).
    zombie_fail_count: u32,
    /// Handle to the BLE notify thread.
    notify_handle: Option<std::thread::JoinHandle<()>>,
    /// Set to `true` after `init()` completes. Guards `process()` and
    /// `is_connected()` against calls before NimBLE stack init.
    initialized: bool,
}

impl BleManager {
    /// Create a new BLE manager (does NOT init BLE hardware).
    ///
    /// The caller must call `init()` separately to start BLE advertising.
    ///
    /// # Arguments
    ///
    /// * `cmd_tx` - Sender for commands received via BLE RX (bounded sync_channel).
    pub fn new(cmd_tx: mpsc::SyncSender<CommandEnvelope>) -> Self {
        Self {
            tx_char: None,
            connected: Arc::new(AtomicBool::new(false)),
            cmd_tx,
            zombie_fail_count: 0,
            notify_handle: None,
            initialized: false,
        }
    }

    /// Initialise BLE hardware.
    ///
    /// Sets BT/WiFi coexistence to prefer BLE, initialises the NimBLE stack,
    /// creates the NUS GATT service with RX (write) and TX (notify) characteristics,
    /// starts advertising as "EcoTiter-XXXX".
    ///
    /// This is a blocking call (init only).
    ///
    /// # Errors
    ///
    /// Returns `NetworkError::BleInitFailed` on any init error.
    pub fn init(&mut self) -> Result<(), NetworkError> {
        // Set BT/WiFi coexistence: prefer BLE for reliable notifications
        // Safety: esp_coex_preference_set is a simple register write that sets
        // the BT/WiFi coexistence priority. Safe to call once at init before
        // any radio activity. Returns ESP_OK on success.
        unsafe {
            if esp_coex_preference_set(esp_coex_prefer_t_ESP_COEX_PREFER_BT) != esp_idf_sys::ESP_OK
            {
                log::warn!("BLE: esp_coex_preference_set failed (non-fatal)");
            }
        }

        // Initialise NimBLE stack — does not return Result
        BLEDevice::init();
        log::info!("BLE: NimBLE stack initialised");

        // Set device name (static method)
        BLEDevice::set_device_name(config::BLE_ADV_NAME_PREFIX)
            .map_err(|_| NetworkError::BleInitFailed)?;

        // Get BLE device instance for instance methods
        let device = BLEDevice::take();

        // High TX power for reliable connection (instance method)
        device
            .set_power(PowerType::Default, PowerLevel::P9)
            .map_err(|_| NetworkError::BleInitFailed)?;

        // Get BLE server (global singleton, never fails)
        let server = device.get_server();

        // Create NUS service
        let service = server.create_service(
            BleUuid::from_uuid128_string(config::NUS_SERVICE_UUID)
                .map_err(|_| NetworkError::BleInitFailed)?,
        );

        // Create RX characteristic (device receives commands from phone)
        let rx_char = service.lock().create_characteristic(
            BleUuid::from_uuid128_string(config::NUS_RX_UUID)
                .map_err(|_| NetworkError::BleInitFailed)?,
            NimbleProperties::WRITE | NimbleProperties::WRITE_NO_RSP,
        );

        // Create TX characteristic (device sends notifications to phone)
        let tx_char = service.lock().create_characteristic(
            BleUuid::from_uuid128_string(config::NUS_TX_UUID)
                .map_err(|_| NetworkError::BleInitFailed)?,
            NimbleProperties::NOTIFY | NimbleProperties::READ,
        );

        // ── RX callback: parse incoming data as CommandEnvelope ──
        let cmd_tx = self.cmd_tx.clone();
        rx_char.lock().on_write(move |args| {
            let data = args.recv_data().to_vec();
            if let Ok(body_str) = core::str::from_utf8(&data) {
                if let Ok(envelope) = serde_json::from_str::<CommandEnvelope>(body_str) {
                    match cmd_tx.try_send(envelope) {
                        Ok(()) => {
                            log::info!("BLE: RX command queued");
                        }
                        Err(mpsc::TrySendError::Full(_)) => {
                            log::warn!("BLE: RX command dropped (queue full)");
                        }
                        Err(mpsc::TrySendError::Disconnected(_)) => {
                            log::error!("BLE: RX command dropped (disconnected)");
                        }
                    }
                } else {
                    log::warn!("BLE: RX invalid JSON");
                }
            } else {
                log::warn!("BLE: RX non-UTF8 data");
            }
        });

        // ── Subscribe callback: track connection state ──
        let connected_flag = Arc::clone(&self.connected);
        tx_char.lock().on_subscribe(move |_char, _desc, sub| {
            let is_subscribed = !sub.is_empty();
            log::info!("BLE: subscribe callback — subscribed={is_subscribed}");
            connected_flag.store(is_subscribed, Ordering::Release);
            G_BLE_CONNECTED.store(is_subscribed, Ordering::Release);
        });

        // Store TX characteristic for later notification
        self.tx_char = Some(tx_char);

        // Start the server
        server.start().map_err(|_| NetworkError::BleInitFailed)?;

        // Start advertising (instance method on device)
        {
            let mut advertising = device.get_advertising().lock();
            let mut adv_data = BLEAdvertisementData::new();
            adv_data.name(config::BLE_ADV_NAME_PREFIX);
            advertising
                .set_data(&mut adv_data)
                .map_err(|_| NetworkError::BleInitFailed)?;
            advertising
                .start()
                .map_err(|_| NetworkError::BleInitFailed)?;
        }

        // Connection parameter update is deferred to NimBLE's automatic parameter
        // negotiation (esp32-nimble handles this internally via the BLE stack).

        log::info!("BLE: advertising as '{}'", config::BLE_ADV_NAME_PREFIX);

        self.initialized = true;

        Ok(())
    }

    /// Send data via BLE notify (TX characteristic).
    ///
    /// # Zombie Defense Level 1 & 3
    ///
    /// - If 5 consecutive notify failures → `zombie_kill()` (Level 1)
    /// - If `connected_count()==0` but local flag is true → kill (Level 3)
    #[allow(unused)]
    fn ble_send(&mut self, data: &[u8]) -> Result<(), ()> {
        let server = BLEDevice::take().get_server();
        let Some(tx_char) = self.tx_char.as_ref() else {
            return Err(());
        };

        // Level 3: check if NimBLE thinks we're connected
        let ble_count = server.connected_count();
        if ble_count == 0 && self.connected.load(Ordering::Acquire) {
            log::warn!("BLE: zombie detected (Level 3) — connected_count=0 but flag=true");
            self.zombie_kill();
            return Err(());
        }

        if !self.connected.load(Ordering::Acquire) {
            return Err(());
        }

        // Notify all subscribed connections
        let mut success = true;
        for conn in server.connections() {
            if tx_char
                .lock()
                .notify_with(data, conn.conn_handle())
                .is_err()
            {
                success = false;
            }
        }

        if success {
            self.zombie_fail_count = 0;
            Ok(())
        } else {
            log::warn!("BLE: notify failed (count={})", self.zombie_fail_count + 1);
            self.zombie_fail_count += 1;

            // Level 1: zombie kill after 5 consecutive failures
            if self.zombie_fail_count >= config::ZOMBIE_NOTIFY_FAIL_LIMIT {
                log::error!(
                    "BLE: zombie detected (Level 1) — {0} consecutive notify failures",
                    self.zombie_fail_count
                );
                self.zombie_kill();
            }

            Err(())
        }
    }

    /// Zombie defense — disconnect, flush, restart advertising.
    ///
    /// Called when any zombie defense level triggers.
    fn zombie_kill(&mut self) {
        log::warn!("BLE: zombie_kill() — disconnecting and restarting advertising");
        self.zombie_fail_count = 0;
        self.connected.store(false, Ordering::Release);
        G_BLE_CONNECTED.store(false, Ordering::Release);

        // Restart advertising (disconnect happens implicitly)
        let mut advertising = BLEDevice::take().get_advertising().lock();
        let _ = advertising.stop();
        if advertising.start().is_ok() {
            log::info!("BLE: advertising restarted");
        } else {
            log::error!("BLE: failed to restart advertising");
        }
    }

    /// Process zombie defense (Level 2).
    ///
    /// Called from the main loop every tick (~10 ms).
    ///
    /// Level 2: If `connected_count() == 0` but `G_BLE_CONNECTED == true`,
    /// the BLE connection is stale — kill it.
    ///
    /// # Safety
    ///
    /// This method is safe to call even before `init()` — if the BLE stack
    /// has not been initialised, it returns immediately without accessing
    /// NimBLE globals.
    pub fn process(&mut self) {
        if !self.initialized {
            return;
        }

        let ble_count = BLEDevice::take().get_server().connected_count();
        let flag = G_BLE_CONNECTED.load(Ordering::Acquire);

        if ble_count == 0 && flag {
            log::warn!(
                "BLE: zombie detected (Level 2) — connected_count=0 but G_BLE_CONNECTED=true"
            );
            self.zombie_kill();
        }

        // Sync local connected flag with global
        if flag != self.connected.load(Ordering::Acquire) {
            self.connected.store(flag, Ordering::Release);
        }
    }

    /// Start the BLE notify thread.
    ///
    /// This thread receives status updates from the main loop and sends them
    /// via BLE notify. Uses an 8KB stack (config::BLE_NOTIFY_THREAD_STACK).
    ///
    /// # Arguments
    ///
    /// * `status_rx` - Receiver for status updates (moved into the thread).
    ///
    /// # Panics
    ///
    /// Panics if called before `init()`, as the NimBLE stack must be initialised
    /// for the notify thread to access BLE characteristics.
    pub fn start_notify_thread(&mut self, status_rx: mpsc::Receiver<StatusUpdate>) {
        assert!(
            self.initialized,
            "BLE: start_notify_thread called before init()"
        );
        let handle = std::thread::Builder::new()
            .stack_size(config::BLE_NOTIFY_THREAD_STACK)
            .name("ble-notify".into())
            .spawn(move || {
                log::info!("BLE: notify thread started");

                loop {
                    if let Ok(_status) = status_rx.recv() {
                        if G_BLE_CONNECTED.load(Ordering::Acquire) {
                            // TODO: serialize status and notify
                        }
                    } else {
                        log::error!("BLE: notify thread — status_rx disconnected");
                        break;
                    }
                }
            });

        match handle {
            Ok(h) => {
                self.notify_handle = Some(h);
            }
            Err(e) => {
                log::error!("BLE: failed to spawn notify thread: {e:?}");
            }
        }
    }

    /// Stop BLE advertising and disconnect.
    pub fn stop(&mut self) {
        if !self.initialized {
            return;
        }
        let advertising = BLEDevice::take().get_advertising().lock();
        let _ = advertising.stop();
        self.connected.store(false, Ordering::Release);
        G_BLE_CONNECTED.store(false, Ordering::Release);
        log::info!("BLE: stopped");
    }

    /// Returns whether BLE is connected to a client.
    ///
    /// Returns `false` if the BLE stack has not been initialised
    /// (before `init()` is called).
    pub fn is_connected(&self) -> bool {
        if !self.initialized {
            return false;
        }
        self.connected.load(Ordering::Acquire)
    }
}

impl Drop for BleManager {
    fn drop(&mut self) {
        self.stop();
    }
}
