#include "common/stats/stats_journal.hpp"

#include <cstring>

namespace eugraph {
namespace {

void putU16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xff));
    out.push_back(static_cast<char>((v >> 8) & 0xff));
}
void putU32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void putU64(std::string& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
bool readU16(std::string_view s, size_t& off, uint16_t& v) {
    if (off + 2 > s.size())
        return false;
    v = static_cast<uint16_t>(static_cast<unsigned char>(s[off])) |
        (static_cast<uint16_t>(static_cast<unsigned char>(s[off + 1])) << 8);
    off += 2;
    return true;
}
bool readU32(std::string_view s, size_t& off, uint32_t& v) {
    if (off + 4 > s.size())
        return false;
    v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(static_cast<unsigned char>(s[off + i])) << (8 * i);
    off += 4;
    return true;
}
bool readU64(std::string_view s, size_t& off, uint64_t& v) {
    if (off + 8 > s.size())
        return false;
    v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(static_cast<unsigned char>(s[off + i])) << (8 * i);
    off += 8;
    return true;
}
void putI64(std::string& out, int64_t v) {
    putU64(out, static_cast<uint64_t>(v));
}
bool readI64(std::string_view s, size_t& off, int64_t& v) {
    uint64_t u = 0;
    if (!readU64(s, off, u))
        return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
}
} // namespace

std::string encodeStatsJournalKey(uint64_t seq) {
    std::string key;
    key.reserve(8);
    for (int i = 0; i < 8; ++i)
        key.push_back(static_cast<char>((seq >> (8 * (7 - i))) & 0xff)); // big-endian
    return key;
}

std::string encodeStatsDelta(const StatsDelta& delta) {
    std::string out;
    out.push_back(1); // version
    putU32(out, static_cast<uint32_t>(delta.vertex_delta.size()));
    for (const auto& [id, d] : delta.vertex_delta) {
        putU16(out, id);
        putI64(out, d);
    }
    putU32(out, static_cast<uint32_t>(delta.edge_delta.size()));
    for (const auto& [id, d] : delta.edge_delta) {
        putU16(out, id);
        putI64(out, d);
    }
    putU32(out, static_cast<uint32_t>(delta.index_delta.size()));
    for (const auto& [id, d] : delta.index_delta) {
        putU32(out, id);
        putI64(out, d);
    }
    return out;
}

bool decodeStatsDelta(std::string_view bytes, StatsDelta& out) {
    if (bytes.empty() || static_cast<unsigned char>(bytes[0]) != 1)
        return false;
    size_t off = 1;
    uint32_t n = 0;
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t id;
        int64_t d;
        if (!readU16(bytes, off, id) || !readI64(bytes, off, d))
            return false;
        out.vertex_delta[id] += d;
    }
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t id;
        int64_t d;
        if (!readU16(bytes, off, id) || !readI64(bytes, off, d))
            return false;
        out.edge_delta[id] += d;
    }
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t id;
        int64_t d;
        if (!readU32(bytes, off, id) || !readI64(bytes, off, d))
            return false;
        out.index_delta[id] += d;
    }
    return off == bytes.size();
}

} // namespace eugraph
