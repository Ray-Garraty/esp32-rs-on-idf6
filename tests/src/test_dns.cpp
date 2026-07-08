#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <array>

#include "domain/dns.hpp"

using namespace ecotiter::domain;

// Helper: build a minimal DNS query for a given domain name
// Buffer is 256 bytes to accommodate long captive portal probe domains
// like "connectivitycheck.gstatic.com"
static std::array<uint8_t, 256> makeQuery(const char* name, uint16_t id = 0x1234) {
    std::array<uint8_t, 256> buf{};
    auto* hdr = reinterpret_cast<DnsHeader*>(buf.data());
    hdr->id = __builtin_bswap16(id);
    hdr->flags = __builtin_bswap16(0x0100); // standard query
    hdr->qdcount = __builtin_bswap16(1);

    size_t off = sizeof(DnsHeader);
    // Encode name
    while (*name) {
        const char* dot = std::strchr(name, '.');
        size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
        buf[off++] = static_cast<uint8_t>(len);
        std::memcpy(buf.data() + off, name, len);
        off += len;
        name = dot ? dot + 1 : name + len;
    }
    buf[off++] = 0; // root label
    // QTYPE = A (1), QCLASS = IN (1)
    buf[off++] = 0; buf[off++] = 1; // type A
    buf[off++] = 0; buf[off++] = 1; // class IN

    return buf;
}

TEST_CASE("DNS: builds valid response for A query", "[dns]") {
    auto query = makeQuery("example.com", 0xABCD);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    REQUIRE(*result > sizeof(DnsHeader));

    // Parse response header
    const auto* rsp = reinterpret_cast<const DnsHeader*>(response.data());
    REQUIRE(rsp->id == __builtin_bswap16(0xABCD));
    REQUIRE(__builtin_bswap16(rsp->flags) == 0x8180); // response + authoritative
    REQUIRE(__builtin_bswap16(rsp->qdcount) == 1);
    REQUIRE(__builtin_bswap16(rsp->ancount) == 1);

    // Verify answer section
    const auto* answer = reinterpret_cast<const DnsAnswer*>(
        response.data() + *result - sizeof(DnsAnswer));
    REQUIRE(__builtin_bswap16(answer->name_ptr) == 0xC00C); // pointer to question
    REQUIRE(__builtin_bswap16(answer->type) == 1);  // A record
    REQUIRE(__builtin_bswap16(answer->klass) == 1); // IN class
    REQUIRE(__builtin_bswap16(answer->rdlength) == 4);
    REQUIRE(answer->rdata == 0x0104A8C0); // 192.168.4.1
}

TEST_CASE("DNS: returns NXDOMAIN for zero questions", "[dns]") {
    std::array<uint8_t, sizeof(DnsHeader)> query{};
    auto* hdr = reinterpret_cast<DnsHeader*>(query.data());
    hdr->id = __builtin_bswap16(0x0001);
    hdr->flags = __builtin_bswap16(0x0100);
    hdr->qdcount = 0; // no questions

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    // BuildDnsResponse will return failure for 0 questions
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("DNS: rejects too short query", "[dns]") {
    uint8_t tiny[5] = {};

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(tiny, sizeof(tiny), response, 0x0104A8C0);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == DnsError::QueryTooShort);
}

TEST_CASE("DNS: preserves original query ID", "[dns]") {
    auto query = makeQuery("google.com", 0xDEAD);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    const auto* rsp = reinterpret_cast<const DnsHeader*>(response.data());
    REQUIRE(rsp->id == __builtin_bswap16(0xDEAD));
}

// Captive portal regression tests
// Verifies that DNS responses point to AP_IP (192.168.4.1) for captive portal detection

TEST_CASE("CaptivePortal: DNS responds with AP IP for captive.apple.com", "[captive][dns]") {
    // captive.apple.com is used by Apple devices for captive portal detection
    auto query = makeQuery("captive.apple.com", 0x4321);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    const auto* answer = reinterpret_cast<const DnsAnswer*>(
        response.data() + *result - sizeof(DnsAnswer));
    // Verify answer IP is 192.168.4.1 (AP_IP in network byte order)
    REQUIRE(answer->rdata == 0x0104A8C0);
}

TEST_CASE("CaptivePortal: DNS responds with AP IP for connectivitycheck.gstatic.com",
          "[captive][dns]") {
    // Used by Android/Chrome for captive portal detection
    auto query = makeQuery("connectivitycheck.gstatic.com", 0x5678);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    const auto* answer = reinterpret_cast<const DnsAnswer*>(
        response.data() + *result - sizeof(DnsAnswer));
    REQUIRE(answer->rdata == 0x0104A8C0);
}

TEST_CASE("CaptivePortal: DNS responds with AP IP for any domain", "[captive][dns]") {
    // Verifies that all DNS queries resolve to the captive portal IP,
    // which is the core behavior needed for captive portal interception.
    // The ESP32's DNS responder should always return 192.168.4.1.
    auto query = makeQuery("msftncsi.com", 0x9ABC);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    const auto* answer = reinterpret_cast<const DnsAnswer*>(
        response.data() + *result - sizeof(DnsAnswer));
    REQUIRE(answer->rdata == 0x0104A8C0);
}

TEST_CASE("CaptivePortal: DNS response flag is standard (0x8180)", "[captive][dns]") {
    // Verify the response flags indicate a standard response (no error)
    auto query = makeQuery("example.com", 0x1111);

    memory::DnsBuf response{};
    auto result = tryBuildDnsResponse(query.data(), query.size(), response, 0x0104A8C0);

    REQUIRE(result.has_value());
    const auto* rsp = reinterpret_cast<const DnsHeader*>(response.data());
    REQUIRE(__builtin_bswap16(rsp->flags) == 0x8180);
    REQUIRE(__builtin_bswap16(rsp->ancount) == 1);
    REQUIRE(__builtin_bswap16(rsp->arcount) == 0);
}
