//! REST API route handler function signatures.
//!
//! These functions are called by the EspHttpServer route handlers (Phase 4).
//! They build JSON responses using `CompactJson` / `heapless::String` buffers.
//!
//! The actual EspHttpServer registration will be in `infrastructure/network/http_server.rs`.

use core::fmt::Write as CoreWrite;

use crate::domain::memory::MAX_RESPONSE_SIZE;
use heapless::String;

/// Handle GET /api/ping — health check.
pub fn handle_api_ping() -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(buf, r#"{{"status":"ok"}}"#);
    buf
}

/// Handle GET /api/status — full device status broadcast.
///
/// Builds a `BroadcastEvent` and serializes it.
/// This is a stub — real implementation reads hardware state in Phase 5.
pub fn handle_api_status() -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(
        buf,
        r#"{{"ts":0,"temp":null,"mv":0,"vlv":"in","brt":{{"sts":"idle","vl":0.0,"spd":0.0}}}}"#
    );
    buf
}

/// Handle POST /api/command — execute a JSON command.
///
/// Parses the command, dispatches it, and returns the serialized response.
///
/// # Arguments
///
/// * `body` - Raw JSON bytes from the request body.
///
/// This is a stub that returns a placeholder response.
/// Full dispatch integration is in Phase 5.
// Stub: if-let is more readable than map_or_else with divergent branch bodies.
#[allow(clippy::option_if_let_else)]
pub fn handle_api_command(body: &[u8]) -> String<MAX_RESPONSE_SIZE> {
    // Try to parse the body as a CommandEnvelope
    let body_str = core::str::from_utf8(body).unwrap_or("");
    if let Ok(_envelope) =
        serde_json::from_str::<crate::application::command::CommandEnvelope>(body_str)
    {
        // Stub: acknowledge receipt. Full dispatch in Phase 5.
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"ok","message":"received"}}"#);
        buf
    } else {
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"error","message":"Invalid JSON"}}"#);
        buf
    }
}

/// Handle GET /api/valve/state — current valve position.
pub fn handle_valve_state() -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(buf, r#"{{"status":"ok","data":{{"position":"input"}}}}"#);
    buf
}

/// Handle POST /api/valve/set — set valve position.
// Stub: if-let is more readable than map_or_else with divergent branch bodies.
#[allow(clippy::option_if_let_else)]
pub fn handle_valve_set(body: &[u8]) -> String<MAX_RESPONSE_SIZE> {
    let body_str = core::str::from_utf8(body).unwrap_or("");
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(body_str) {
        let pos = val["position"].as_str().unwrap_or("input");
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"ok","data":{{"position":"{pos}"}}}}"#);
        buf
    } else {
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"error","message":"Invalid JSON"}}"#);
        buf
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_api_ping() {
        let resp = handle_api_ping();
        assert_eq!(resp.as_str(), r#"{"status":"ok"}"#);
    }

    #[test]
    fn test_api_status() {
        let resp = handle_api_status();
        assert!(resp.contains(r#""sts":"idle""#));
    }

    #[test]
    fn test_api_command_valid() {
        let body = br#"{"id":1,"cmd":"serial.ping"}"#;
        let resp = handle_api_command(body);
        assert!(resp.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_api_command_invalid_json() {
        let body = b"not json";
        let resp = handle_api_command(body);
        assert!(resp.contains(r#""status":"error""#));
    }

    #[test]
    fn test_valve_state() {
        let resp = handle_valve_state();
        assert!(resp.contains(r#""position":"input""#));
    }

    #[test]
    fn test_valve_set() {
        let body = br#"{"position":"output"}"#;
        let resp = handle_valve_set(body);
        assert!(resp.contains(r#""position":"output""#));
    }
}
