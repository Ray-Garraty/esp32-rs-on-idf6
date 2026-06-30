//! DS18B20 temperature sensor via software bitbang OneWire protocol.
//!
//! Pin: GPIO33 (PinDriver::input_output_od with Pull::Up).
//!
//! This module provides:
//! - `OneWireBus` — low-level bitbang protocol (reset, read/write byte,
//!   skip_rom, convert_t, read_scratchpad).
//! - `read_sensor()` — full DS18B20 read sequence returning `Option<f32>`.
//! - `temp_celsius()` — thread-safe access to the last-read value.
//!
//! The temperature read runs in a dedicated `std::thread` (16 KB stack) at
//! 1-second intervals. The main loop reads the atomic via `temp_celsius()`.
//!
//! Timing is via `Ets::delay_us()` — not cycle-accurate but within the
//! DS18B20's forgiving timing tolerances (1–10 µs).

use core::sync::atomic::{AtomicI32, Ordering};

use esp_idf_hal::delay::Ets;
use esp_idf_hal::gpio::{InputOutput, InputPin, OutputPin, PinDriver, Pull};

use log::warn;

use crate::config;

/// Sentinel value indicating "sensor disconnected".
const DISCONNECTED: i32 = i32::MIN;

/// Last valid temperature reading in hundredths of a degree Celsius
/// (e.g., 2350 = 23.50 °C). Initialised to `DISCONNECTED`.
static TEMP_C_X100: AtomicI32 = AtomicI32::new(DISCONNECTED);

/// Return the last valid temperature reading, if any.
///
/// Returns `None` if the sensor has never been read or is disconnected.
#[allow(clippy::cast_precision_loss)]
pub fn temp_celsius() -> Option<f32> {
    let v = TEMP_C_X100.load(Ordering::Relaxed);
    if v == DISCONNECTED {
        None
    } else {
        Some(v as f32 / 100.0)
    }
}

/// Clear the temperature reading (set to disconnected).
pub fn clear() {
    TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
}

/// Software bitbang OneWire bus master.
///
/// Implements the DS18B20 protocol using `Ets::delay_us` for precise timing.
/// Timings follow the Dallas Semiconductor DS18B20 datasheet.
pub struct OneWireBus {
    pin: PinDriver<'static, InputOutput>,
}

impl OneWireBus {
    /// Create a new OneWire bus on the given GPIO pin.
    ///
    /// The pin is configured as open-drain with internal pull-up, then set HIGH.
    ///
    /// # Errors
    ///
    /// Returns `EspError` if the GPIO initialisation fails.
    pub fn new(pin: impl InputPin + OutputPin + 'static) -> Result<Self, esp_idf_sys::EspError> {
        let mut pin = PinDriver::input_output_od(pin, Pull::Up)?;
        pin.set_high()?;
        Ok(Self { pin })
    }

    /// Reset the OneWire bus.
    ///
    /// Returns `true` if a device presence pulse was detected.
    pub fn reset(&mut self) -> bool {
        self.pin.set_low().ok();
        Ets::delay_us(480);
        self.pin.set_high().ok();
        Ets::delay_us(75);
        let present = self.pin.is_low();
        Ets::delay_us(405);
        present
    }

    /// Write a single byte to the OneWire bus.
    pub fn write_byte(&mut self, byte: u8) {
        for i in 0..8 {
            if (byte >> i) & 1 != 0 {
                self.write_bit_1();
            } else {
                self.write_bit_0();
            }
        }
    }

    /// Write a logic 1 bit.
    fn write_bit_1(&mut self) {
        self.pin.set_low().ok();
        Ets::delay_us(6);
        self.pin.set_high().ok();
        Ets::delay_us(64);
    }

    /// Write a logic 0 bit.
    fn write_bit_0(&mut self) {
        self.pin.set_low().ok();
        Ets::delay_us(60);
        self.pin.set_high().ok();
        Ets::delay_us(10);
    }

    /// Read a single byte from the OneWire bus.
    pub fn read_byte(&mut self) -> u8 {
        let mut byte = 0u8;
        for i in 0..8 {
            if self.read_bit() {
                byte |= 1 << i;
            }
        }
        byte
    }

    /// Read a single bit from the OneWire bus.
    fn read_bit(&mut self) -> bool {
        self.pin.set_low().ok();
        Ets::delay_us(3);
        self.pin.set_high().ok();
        Ets::delay_us(10);
        let bit = self.pin.is_high();
        Ets::delay_us(53);
        bit
    }

    /// Send the Skip ROM command (0xCC) — assumes a single device on the bus.
    pub fn skip_rom(&mut self) {
        self.write_byte(0xCC);
    }

    /// Send the Convert T command (0x44) to start a temperature conversion.
    pub fn convert_t(&mut self) {
        self.write_byte(0x44);
    }

    /// Read the 9-byte scratchpad (command 0xBE).
    ///
    /// Returns bytes: temp LSB, temp MSB, TH, TL, config, reserved[3], CRC.
    pub fn read_scratchpad(&mut self) -> [u8; 9] {
        self.write_byte(0xBE);
        let mut buf = [0u8; 9];
        for byte in &mut buf {
            *byte = self.read_byte();
        }
        buf
    }
}

// SAFETY: PinDriver<InputOutput> contains a raw pointer to the GPIO
// peripheral's MMIO register block, which lives at a fixed address.
// Moving this value between threads is safe because the register address
// is identical on all ESP32 cores. No thread-local state is involved.
unsafe impl Send for OneWireBus {}

/// Perform a complete DS18B20 read sequence:
/// reset → skip_rom → convert_t → wait → reset → skip_rom → read_scratchpad.
///
/// On success, stores the temperature in `TEMP_C_X100` and returns `Some(f32)`.
/// On failure (no presence pulse, lost during conversion, out of range),
/// stores `DISCONNECTED` and returns `None`.
///
/// # Blocking
///
/// This function blocks for ~800 ms waiting for the temperature conversion.
/// It MUST be called from a dedicated thread (16 KB stack), not the main loop.
pub fn read_sensor(bus: &mut OneWireBus) -> Option<f32> {
    if !bus.reset() {
        warn!("DS18B20 not detected (no presence pulse)");
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    bus.skip_rom();
    bus.convert_t();

    // Wait for conversion to complete (max 750 ms for 12-bit resolution)
    std::thread::sleep(std::time::Duration::from_millis(
        config::TEMP_CONVERSION_WAIT_MS,
    ));

    if !bus.reset() {
        warn!("DS18B20 lost during conversion");
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    bus.skip_rom();
    let buf = bus.read_scratchpad();

    let lsb = buf[0];
    let msb = buf[1];
    let temp_raw = u16::from(msb) << 8 | u16::from(lsb);
    let temp = f32::from(temp_raw) / 16.0;

    if !(-55.0..=125.0).contains(&temp) {
        warn!("DS18B20 out of range: {temp}°C");
        TEMP_C_X100.store(DISCONNECTED, Ordering::Relaxed);
        return None;
    }

    #[allow(clippy::cast_possible_truncation)]
    TEMP_C_X100.store((temp * 100.0) as i32, Ordering::Relaxed);
    Some(temp)
}
