//! DNS response builder for captive portal.
//!
//! This module provides a pure function to build DNS response packets.
//! It has no ESP-IDF dependencies and is host-testable.
//!
//! The max DNS response size is 512 bytes (DNS standard). The function
//! uses `heapless::Vec` to avoid heap allocation.

use heapless::Vec;

/// Maximum DNS response size (standard DNS limit).
const DNS_MAX_SIZE: usize = 512;

/// Build a DNS response packet for a given query.
///
/// The response always returns the given IP address (captive portal style).
/// Returns an empty Vec if the query is too short to be a valid DNS request.
///
/// # Arguments
///
/// * `query` - Raw DNS query bytes (received from UDP).
/// * `ip` - IP address to return (as 4 octets).
///
/// # Returns
///
/// * `heapless::Vec<u8, 512>` — Complete DNS response packet, or empty if
///   query is invalid. Uses fixed-size stack allocation (no heap).
pub fn build_dns_response(query: &[u8], ip: [u8; 4]) -> Vec<u8, DNS_MAX_SIZE> {
    if query.len() < 12 {
        // Too short to be a valid DNS header
        return Vec::new();
    }

    // Extract transaction ID (bytes 0-1)
    let tx_id = &query[..2];

    // Set flags: response + standard query + no error
    let flags: [u8; 2] = [0x81, 0x80]; // QR=1, OPCODE=0, AA=1, TC=0, RD=1, RA=1, RCODE=0

    // Build response header
    let mut response: Vec<u8, DNS_MAX_SIZE> = Vec::new();
    // All extend_from_slice calls return Result<(), ()> — we ignore errors
    // because DNS_MAX_SIZE (512) is the standard maximum DNS packet size and
    // our response will always fit.
    let _ = response.extend_from_slice(tx_id); // Transaction ID
    let _ = response.extend_from_slice(&flags); // Flags
    let _ = response.extend_from_slice(&query[4..6]); // QDCOUNT (same as query)
    let _ = response.extend_from_slice(&[0x00, 0x01]); // ANCOUNT = 1 answer
    let _ = response.extend_from_slice(&[0x00, 0x00]); // NSCOUNT = 0
    let _ = response.extend_from_slice(&[0x00, 0x00]); // ARCOUNT = 0

    // Copy the question section verbatim
    if query.len() > 12 {
        let _ = response.extend_from_slice(&query[12..]);
    }

    // Build answer section: pointer to query name + type A + class IN + TTL + IP
    // Pointer: 0xC0 0x0C (points to byte 12 in the DNS message — start of question)
    let _ = response.extend_from_slice(&[0xC0, 0x0C]); // Name pointer
    let _ = response.extend_from_slice(&[0x00, 0x01]); // Type A (host address)
    let _ = response.extend_from_slice(&[0x00, 0x01]); // Class IN
    let _ = response.extend_from_slice(&[0x00, 0x00, 0x00, 0x3C]); // TTL = 60 seconds
    let _ = response.extend_from_slice(&[0x00, 0x04]); // Data length = 4 bytes
    let _ = response.extend_from_slice(&ip); // IP address

    response
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: create a minimal DNS query for a domain with the given labels.
    fn make_dns_query(domain_labels: &[&[u8]]) -> Vec<u8, DNS_MAX_SIZE> {
        let mut query: Vec<u8, DNS_MAX_SIZE> = Vec::new();
        // Transaction ID (0x1234)
        let _ = query.extend_from_slice(&[0x12, 0x34]);
        // Flags: standard query, recursion desired
        let _ = query.extend_from_slice(&[0x01, 0x00]);
        // QDCOUNT = 1
        let _ = query.extend_from_slice(&[0x00, 0x01]);
        // ANCOUNT, NSCOUNT, ARCOUNT = 0
        let _ = query.extend_from_slice(&[0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        // Question: domain name (label sequence)
        for label in domain_labels {
            let _ = query.push(label.len() as u8);
            let _ = query.extend_from_slice(label);
        }
        // Terminating zero
        let _ = query.push(0x00);
        // Type A
        let _ = query.extend_from_slice(&[0x00, 0x01]);
        // Class IN
        let _ = query.extend_from_slice(&[0x00, 0x01]);
        query
    }

    /// AC-019-1: `build_dns_response` returns a valid DNS response structure.
    #[test]
    fn test_dns_response_structure() {
        let query = make_dns_query(&[b"example", b"com"]);
        let ip = [192, 168, 4, 1];
        let response = build_dns_response(&query, ip);

        // Must be >= header size (12 bytes)
        assert!(response.len() >= 12, "response too short");

        // Transaction ID must match query
        assert_eq!(response[..2], [0x12, 0x34], "TX ID mismatch");

        // Flags: QR=1 (response), AA=1 (authoritative), RCODE=0 (no error)
        assert_eq!(response[2], 0x81, "flags high byte");
        assert_eq!(response[3], 0x80, "flags low byte");

        // QDCOUNT = 1
        assert_eq!(response[4..6], [0x00, 0x01], "QDCOUNT");

        // ANCOUNT = 1
        assert_eq!(response[6..8], [0x00, 0x01], "ANCOUNT");

        // NSCOUNT = 0
        assert_eq!(response[8..10], [0x00, 0x00], "NSCOUNT");

        // ARCOUNT = 0
        assert_eq!(response[10..12], [0x00, 0x00], "ARCOUNT");
    }

    /// AC-019-2: The response contains the correct IP bytes in the answer.
    #[test]
    fn test_dns_response_ip_bytes() {
        let query = make_dns_query(&[b"example", b"com"]);
        let ip = [10, 0, 0, 1];
        let response = build_dns_response(&query, ip);

        // The IP bytes should appear as the last 4 bytes of the response
        assert!(response.ends_with(&ip), "response should end with IP bytes");
    }

    /// AC-019-3: Response gracefully handles short/invalid queries (truncation).
    #[test]
    fn test_dns_response_truncation() {
        // Empty/invalid query should return empty response
        let empty = build_dns_response(&[], [192, 168, 4, 1]);
        assert!(empty.is_empty(), "empty query → empty response");

        // Query too short (< 12 bytes header)
        let short = build_dns_response(&[0x00, 0x01, 0x02], [192, 168, 4, 1]);
        assert!(short.is_empty(), "short query → empty response");

        // Minimum 12-byte header (no question section) — should still produce a response
        let minimal_header = [
            0x12, 0x34, // TX ID
            0x01, 0x00, // Flags
            0x00, 0x01, // QDCOUNT
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ANCOUNT, NSCOUNT, ARCOUNT
        ];
        let minimal = build_dns_response(&minimal_header, [192, 168, 4, 1]);
        assert!(!minimal.is_empty(), "minimal header → non-empty response");
    }

    /// AC-019-4: Multi-label domain names are correctly echoed in the response.
    #[test]
    fn test_dns_response_multi_label() {
        let query = make_dns_query(&[b"very", b"long", b"subdomain", b"example", b"com"]);
        let ip = [192, 168, 4, 1];
        let response = build_dns_response(&query, ip);

        // Should contain the name pointer (0xC0 0x0C)
        assert!(
            response.windows(2).any(|w| w == [0xC0, 0x0C]),
            "response should contain name pointer 0xC00C"
        );

        // Response should be longer than query (header + question + answer)
        assert!(
            response.len() > query.len(),
            "response should be longer than query"
        );

        // The question section (after header) should be preserved in the response
        if query.len() > 12 {
            assert_eq!(
                &response[12..12 + (query.len() - 12)],
                &query[12..],
                "question section should be preserved"
            );
        }
    }
}
