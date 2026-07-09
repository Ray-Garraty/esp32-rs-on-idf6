#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <expected>

#include "domain/memory.hpp"

namespace ecotiter::domain {

#pragma pack(push, 1)
struct DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

struct DnsAnswer {
    uint16_t name_ptr;
    uint16_t type;
    uint16_t klass;
    uint32_t ttl;
    uint16_t rdlength;
    uint32_t rdata;
};
#pragma pack(pop)

enum class DnsError : uint8_t {
    QueryTooShort,
    QueryTruncated,
    BufferTooSmall,
    NotAQuery,
};

[[nodiscard]] inline size_t buildDnsResponse(
    const uint8_t* query, size_t queryLen,
    uint8_t* response, size_t responseCapacity,
    uint32_t answerIp) {

    if (queryLen < sizeof(DnsHeader)) return 0;

    const auto* reqHdr = reinterpret_cast<const DnsHeader*>(query);
    size_t hdrSize = sizeof(DnsHeader);

    uint16_t qdcount = __builtin_bswap16(reqHdr->qdcount);
    if (qdcount == 0) return 0;

    const uint8_t* qptr = query + hdrSize;
    const uint8_t* qend = query + queryLen;

    while (qptr < qend) {
        uint8_t len = *qptr;
        if (len == 0) {
            ++qptr;
            break;
        }
        qptr += 1 + len;
    }
    if (qptr + 4 > qend) return 0;

    size_t questionLen = static_cast<size_t>(qptr - (query + hdrSize));
    size_t totalLen = hdrSize + questionLen + sizeof(DnsAnswer);

    if (totalLen > responseCapacity) return 0;

    auto* hdr = reinterpret_cast<DnsHeader*>(response);
    hdr->id = reqHdr->id;
    hdr->flags = __builtin_bswap16(0x8180);
    hdr->qdcount = reqHdr->qdcount;
    hdr->ancount = __builtin_bswap16(1);
    hdr->nscount = 0;
    hdr->arcount = 0;

    std::memcpy(response + hdrSize, query + hdrSize, questionLen);

    auto* answer = reinterpret_cast<DnsAnswer*>(response + hdrSize + questionLen);
    answer->name_ptr = __builtin_bswap16(0xC00C);
    answer->type = __builtin_bswap16(1);
    answer->klass = __builtin_bswap16(1);
    answer->ttl = __builtin_bswap32(60);
    answer->rdlength = __builtin_bswap16(4);
    answer->rdata = answerIp;

    return totalLen;
}

[[nodiscard]] inline std::expected<size_t, DnsError> tryBuildDnsResponse(
    const uint8_t* query, size_t queryLen,
    memory::DnsBuf& response,
    uint32_t answerIp) {

    if (queryLen < sizeof(DnsHeader)) {
        return std::unexpected(DnsError::QueryTooShort);
    }

    size_t written = buildDnsResponse(query, queryLen,
                                       response.data(), response.size(),
                                       answerIp);
    if (written == 0) {
        return std::unexpected(DnsError::BufferTooSmall);
    }
    return written;
}

/// Extract the queried domain name from a DNS query for diagnostic logging.
/// Writes the domain name (e.g., "captive.apple.com") into outBuf.
/// Returns the number of bytes written (excluding null terminator), or 0 on error.
/// outBuf is always null-terminated when outCap > 0 and parsing succeeds.
[[nodiscard]] inline size_t extractDomainName(
    const uint8_t* query, size_t queryLen,
    char* outBuf, size_t outCap) {

    if (queryLen < sizeof(DnsHeader) || outCap == 0) return 0;

    const uint8_t* qptr = query + sizeof(DnsHeader);
    const uint8_t* qend = query + queryLen;
    size_t written = 0;
    bool first = true;

    while (qptr < qend) {
        uint8_t len = *qptr;

        // Zero-length label marks end of domain name
        if (len == 0) {
            ++qptr;
            break;
        }

        // Bounds check: need len data bytes after the length byte
        if (qptr + 1 + len > qend) {
            if (outCap > 0) outBuf[0] = '\0';
            return 0;
        }

        // Dot separator between labels
        if (!first) {
            if (written + 1 >= outCap) {
                if (outCap > 0) outBuf[0] = '\0';
                return 0;
            }
            outBuf[written++] = '.';
        }
        first = false;

        // Copy label bytes
        if (written + len >= outCap) {
            if (outCap > 0) outBuf[0] = '\0';
            return 0;
        }
        std::memcpy(outBuf + written, qptr + 1, len);
        written += len;
        qptr += 1 + len;
    }

    outBuf[written] = '\0';
    return written;
}

} // namespace ecotiter::domain
