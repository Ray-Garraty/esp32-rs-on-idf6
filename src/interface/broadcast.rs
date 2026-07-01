//! Broadcast event types and serialization for WS / BLE notify.
//!
//! The broadcast event is a snapshot of device state sent every 300 ms
//! to all connected clients (WS and BLE GATT notify).

#![forbid(unsafe_code)]
use crate::domain::memory::MAX_RESPONSE_SIZE;
use crate::domain::types::ValvePosition;
use core::fmt::Write as CoreWrite;
use heapless::String;

/// Burette broadcast sub-object.
pub struct BuretteBroadcast {
    /// Status string: "idle", "working", "error".
    pub sts: &'static str,
    /// Volume in millilitres.
    pub vl: f32,
    /// Speed in ml/min.
    pub spd: f32,
}

/// Full broadcast event — a snapshot of all device state.
pub struct BroadcastEvent {
    /// Milliseconds since boot.
    pub ts: u64,
    /// Temperature in Celsius, `None` when sensor disconnected.
    pub temp: Option<f32>,
    /// Calibrated ADC millivolts.
    pub mv: i16,
    /// Current valve position.
    pub vlv: ValvePosition,
    /// Burette status sub-object.
    pub brt: BuretteBroadcast,
}

/// Serialise a broadcast event into a fixed-size JSON string.
///
/// The JSON format matches the WS event data expected by the WebUI:
/// ```json
/// {"ts":12345,"temp":23.5,"mv":1500,"vlv":"in","brt":{"sts":"idle","vl":0.0,"spd":0.0}}
/// ```
pub fn serialize_broadcast(event: &BroadcastEvent) -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(buf, r#"{{"ts":{},"temp":"#, event.ts);
    match event.temp {
        Some(t) => {
            let _ = write!(buf, "{t:.1}");
        }
        None => {
            let _ = write!(buf, "null");
        }
    }
    let _ = write!(
        buf,
        r#","mv":{},"vlv":"{}","brt":{{"sts":"{}","vl":{:.1},"spd":{:.1}}}}}"#,
        event.mv,
        event.vlv.as_str(),
        event.brt.sts,
        event.brt.vl,
        event.brt.spd,
    );
    buf
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_serialize_broadcast_with_temp() {
        let event = BroadcastEvent {
            ts: 12345,
            temp: Some(23.5),
            mv: 1500,
            vlv: ValvePosition::Input,
            brt: BuretteBroadcast {
                sts: "idle",
                vl: 0.0,
                spd: 0.0,
            },
        };
        let json = serialize_broadcast(&event);
        assert!(json.contains(r#""ts":12345"#));
        assert!(json.contains(r#""temp":23.5"#));
        assert!(json.contains(r#""mv":1500"#));
        assert!(json.contains(r#""vlv":"in""#));
        assert!(json.contains(r#""brt""#));
        assert!(json.contains(r#""sts":"idle""#));
    }

    #[test]
    fn test_serialize_broadcast_null_temp() {
        let event = BroadcastEvent {
            ts: 0,
            temp: None,
            mv: 0,
            vlv: ValvePosition::Output,
            brt: BuretteBroadcast {
                sts: "working",
                vl: 5.0,
                spd: 10.0,
            },
        };
        let json = serialize_broadcast(&event);
        assert!(json.contains(r#""temp":null"#));
        assert!(json.contains(r#""vlv":"out""#));
        assert!(json.contains(r#""sts":"working""#));
        assert!(json.contains(r#""vl":5.0"#));
    }

    #[test]
    fn test_broadcast_fits_in_buffer() {
        let event = BroadcastEvent {
            ts: u64::MAX,
            temp: Some(-55.0),
            mv: i16::MAX,
            vlv: ValvePosition::Input,
            brt: BuretteBroadcast {
                sts: "working",
                vl: 50.0,
                spd: 20.0,
            },
        };
        let json = serialize_broadcast(&event);
        // The serialized string should not exceed MAX_RESPONSE_SIZE
        assert!(json.len() <= MAX_RESPONSE_SIZE);
        // Must end with the closing brace
        assert!(json.ends_with('}'));
    }
}
