#include "storage/stats/stats_snapshot.hpp"

#include <cstring>

namespace eugraph {
namespace {
void putU16(std::string& o, uint16_t v) {
    o.push_back(static_cast<char>(v & 0xff));
    o.push_back(static_cast<char>((v >> 8) & 0xff));
}
void putU32(std::string& o, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        o.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void putU64(std::string& o, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        o.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
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
void putI64(std::string& o, int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    putU64(o, u);
}
bool readI64(std::string_view s, size_t& off, int64_t& v) {
    uint64_t u;
    if (!readU64(s, off, u))
        return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
}
} // namespace

std::string encodeStatsSnapshot(const GraphStatsSnapshot& snapshot) {
    std::string out;
    out.push_back(1);
    putU64(out, snapshot.epoch);
    putU64(out, snapshot.applied_journal_seq);
    out.push_back(snapshot.approximate ? 1 : 0);
    putU32(out, static_cast<uint32_t>(snapshot.labels.size()));
    for (const auto& [id, st] : snapshot.labels) {
        putU16(out, id);
        putI64(out, st.vertices);
    }
    putU32(out, static_cast<uint32_t>(snapshot.edge_labels.size()));
    for (const auto& [id, st] : snapshot.edge_labels) {
        putU16(out, id);
        putI64(out, st.edges);
        putU64(out, 0); // reserved for avg degree encoding version
    }
    putU32(out, static_cast<uint32_t>(snapshot.indexes.size()));
    for (const auto& [id, st] : snapshot.indexes) {
        putU32(out, id);
        putI64(out, st.rows);
        putI64(out, st.null_keys);
        out.push_back(st.ready ? 1 : 0);
    }
    return out;
}

bool decodeStatsSnapshot(std::string_view bytes, GraphStatsSnapshot& out) {
    if (bytes.empty() || static_cast<unsigned char>(bytes[0]) != 1)
        return false;
    size_t off = 1;
    if (!readU64(bytes, off, out.epoch) || !readU64(bytes, off, out.applied_journal_seq))
        return false;
    out.approximate = bytes[off++] != 0;
    uint32_t n = 0;
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t id;
        int64_t v;
        if (!readU16(bytes, off, id) || !readI64(bytes, off, v))
            return false;
        out.labels[id].vertices = v;
    }
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t id;
        int64_t edges;
        uint64_t reserved;
        if (!readU16(bytes, off, id) || !readI64(bytes, off, edges) || !readU64(bytes, off, reserved))
            return false;
        out.edge_labels[id].edges = edges;
    }
    if (!readU32(bytes, off, n))
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t id;
        int64_t rows;
        int64_t null_keys;
        if (!readU32(bytes, off, id) || !readI64(bytes, off, rows) || !readI64(bytes, off, null_keys))
            return false;
        if (off >= bytes.size())
            return false;
        out.indexes[id].rows = rows;
        out.indexes[id].null_keys = null_keys;
        out.indexes[id].ready = bytes[off++] != 0;
    }
    return off == bytes.size();
}

} // namespace eugraph
