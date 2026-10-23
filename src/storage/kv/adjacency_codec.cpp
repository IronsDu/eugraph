#include "storage/kv/adjacency_codec.hpp"

namespace eugraph {
namespace {

void appendU16LE(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void appendU32LE(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

void appendU64LE(std::string& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

uint16_t readU16LE(std::string_view data, size_t off) {
    return static_cast<uint16_t>(static_cast<uint8_t>(data[off]) |
                                 (static_cast<uint16_t>(static_cast<uint8_t>(data[off + 1])) << 8));
}

uint32_t readU32LE(std::string_view data, size_t off) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(static_cast<uint8_t>(data[off + i])) << (i * 8);
    return v;
}

uint64_t readU64LE(std::string_view data, size_t off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(static_cast<uint8_t>(data[off + i])) << (i * 8);
    return v;
}

} // namespace

std::string AdjacencyCodec::encode(const OutEdgeCache& cache) {
    std::string out;
    out.reserve(kHeaderSize + kEntrySize * cache.edges.size());
    out.push_back(static_cast<char>(cache.state));
    for (const auto& edge : cache.edges) {
        appendU16LE(out, edge.edge_label_id);
        appendU16LE(out, 0);
        appendU32LE(out, 0);
        appendU64LE(out, edge.neighbor_id);
        appendU64LE(out, edge.seq);
        appendU64LE(out, edge.edge_id);
    }
    return out;
}

std::optional<OutEdgeCache> AdjacencyCodec::decode(std::string_view data) {
    if (data.size() < kHeaderSize)
        return std::nullopt;
    if ((data.size() - kHeaderSize) % kEntrySize != 0)
        return std::nullopt;

    const uint8_t raw_state = static_cast<uint8_t>(data[0]);
    if (raw_state != static_cast<uint8_t>(AdjacencyCacheState::COMPLETE) &&
        raw_state != static_cast<uint8_t>(AdjacencyCacheState::OVERFLOW))
        return std::nullopt;

    OutEdgeCache cache;
    cache.state = static_cast<AdjacencyCacheState>(raw_state);
    const size_t count = (data.size() - kHeaderSize) / kEntrySize;
    cache.edges.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t off = kHeaderSize + i * kEntrySize;
        if (readU16LE(data, off + 2) != 0 || readU32LE(data, off + 4) != 0)
            return std::nullopt;
        cache.edges.push_back(CachedOutEdge{readU16LE(data, off), readU64LE(data, off + 8), readU64LE(data, off + 16),
                                            readU64LE(data, off + 24)});
    }
    return cache;
}

} // namespace eugraph
