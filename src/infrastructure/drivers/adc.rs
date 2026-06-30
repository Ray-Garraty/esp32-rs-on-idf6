//! ADC driver for pH electrode (ADC1_CH6, GPIO34).
//!
//! Provides:
//! - Oneshot ADC read with DB_12 attenuation.
//! - Rolling average (64 samples via `heapless::Vec` ring buffer).
//! - Calibration via `adc_cal` module (re-exported here).
//! - `calibrated_mv()` = a × raw_mv + b.
//!
//! This matches the prototype (`prototype/src/adc.rs`) pattern exactly,
//! adapted to a struct-based API with proper lifetime management.

use core::sync::atomic::Ordering;

use heapless::Vec;

use esp_idf_hal::adc::attenuation::DB_12;
use esp_idf_hal::adc::oneshot::config::AdcChannelConfig;
use esp_idf_hal::adc::oneshot::{AdcChannelDriver, AdcDriver as EspAdcDriver};
use esp_idf_hal::adc::ADCU1;
use esp_idf_hal::gpio::ADCPin;

use crate::config;
use crate::domain::adc_cal::{COEFF_A_X1000, COEFF_B};
use crate::errors::SensorError;

// Re-export calibration free functions for convenience.
pub use crate::domain::adc_cal::{
    calibrated_from_raw, get_calibration, reset_calibration, set_calibration,
};

// ── Type aliases ───────────────────────────────────────────────

/// Oneshot ADC driver for ADC unit 1.
type EspDriver = EspAdcDriver<'static, ADCU1>;

/// ADC channel type for GPIO34 (ADC1_CH6).
type AdcChan = <esp_idf_hal::gpio::Gpio34<'static> as ADCPin>::AdcChannel;

/// Oneshot ADC channel driver that borrows the ADC driver.
type EspChannel = AdcChannelDriver<'static, AdcChan, &'static EspDriver>;

/// ADC driver for pH electrode.
///
/// Wraps the ESP-IDF oneshot ADC driver with rolling average and
/// atomic calibration coefficients.
///
/// # Lifetime Safety
///
/// `AdcChannelDriver` requires a `&'static AdcDriver` reference, creating a
/// self-referential struct. We satisfy this by leaking the `AdcDriver` via
/// `Box::leak` to obtain a genuine `&'static` reference. The leaked memory
/// lives for the entire program lifetime (singleton peripheral).
pub struct AdcDriver {
    /// Channel borrows the ADC driver via a `'static` reference.
    channel: EspChannel,
    /// Leaked `'static` reference to the ADC driver — must outlive the channel.
    #[allow(dead_code)]
    adc_ref: &'static EspDriver,
    /// Rolling-average ring buffer (up to `ADC_SAMPLES` entries).
    buf: Vec<u16, 64>,
}

impl AdcDriver {
    /// Create a new ADC driver on ADC1_CH6 (GPIO34) with DB_12 attenuation.
    ///
    /// # Errors
    ///
    /// Returns `SensorError::AdcReadFailed` if the ADC peripheral
    /// or channel cannot be initialised.
    pub fn new(
        adc: esp_idf_hal::adc::ADC1<'static>,
        pin: esp_idf_hal::gpio::Gpio34<'static>,
    ) -> Result<Self, SensorError> {
        let adc_driver = Box::new(EspDriver::new(adc)?);
        let adc_ref: &'static EspDriver = Box::leak(adc_driver);

        let channel = EspChannel::new(
            adc_ref,
            pin,
            &AdcChannelConfig {
                attenuation: DB_12,
                ..Default::default()
            },
        )?;

        Ok(Self {
            channel,
            adc_ref,
            buf: Vec::new(),
        })
    }

    /// Perform a single ADC read and return the result in millivolts.
    ///
    /// The reading is also added to the rolling-average buffer.
    ///
    /// # Errors
    ///
    /// Returns `SensorError::AdcReadFailed` if the ADC read fails.
    pub fn read_raw_mv(&mut self) -> Result<u16, SensorError> {
        let raw = self.channel.read()?;

        // Add to rolling average buffer with ring-buffer semantics.
        // When the buffer reaches capacity, restart from empty.
        if self.buf.len() >= config::ADC_SAMPLES as usize {
            self.buf.clear();
        }
        // Safe: we just cleared the buffer, so there is room for one more element.
        self.buf.push(raw).ok();

        Ok(raw)
    }

    /// Return the rolling average of the last N samples, if any.
    ///
    /// Returns `None` if no samples have been read yet.
    #[allow(clippy::cast_possible_truncation)]
    pub fn avg_mv(&self) -> Option<u16> {
        let len = self.buf.len();
        if len == 0 {
            return None;
        }
        let sum: u32 = self.buf.iter().map(|&v| u32::from(v)).sum();
        // Safe: len is non-zero (checked above), and the sum of up to 64 u16
        // values (max 64 × 4095 = 262080) fits easily in u32.
        let count = u32::try_from(len).ok()?;
        Some((sum / count) as u16)
    }

    /// Reset the rolling-average buffer.
    pub fn reset_avg(&mut self) {
        self.buf.clear();
    }

    /// Return the calibrated millivolt value using the latest raw reading
    /// and the current coefficients.
    ///
    /// Formula: `calibrated = a × raw + b`, where `a = COEFF_A_X1000 / 1000`.
    /// Result is clamped to `i16` range.
    #[allow(clippy::cast_possible_truncation)]
    pub fn calibrated_mv(&self) -> i16 {
        let raw = self.buf.last().copied().unwrap_or(0);
        let a = i32::from(COEFF_A_X1000.load(Ordering::Relaxed));
        let b = i32::from(COEFF_B.load(Ordering::Relaxed));
        let result = (a * i32::from(raw)) / 1000 + b;
        result.clamp(i32::from(i16::MIN), i32::from(i16::MAX)) as i16
    }
}
