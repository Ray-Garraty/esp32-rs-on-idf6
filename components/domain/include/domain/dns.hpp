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

} // namespace ecotiter::domain
