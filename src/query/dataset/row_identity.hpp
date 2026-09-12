#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace eugraph {

/// Row identity for DISTINCT.
///
/// Two constraints shape this type:
///  * a chunk is yielded by value and destroyed after each loop iteration, so a
///    key may not hold a reference into it — the snapshot must be taken while
///    the row is being read (a pointer-based key was tried and dangled,
///    surfacing as std::bad_alloc);
///  * materialising a std::vector<Value> per row (the old `toRows()` shape) cost
///    one heap allocation per row purely to be hashed and compared once.
///
/// So the key stores, per column, a numeric digest plus a flag saying whether
/// that digest is an entity id. Column order is preserved, so equality is exact
/// element-wise equality of the snapshot.
struct RowDigest {
    std::vector<uint64_t> cells;
    std::vector<uint8_t> is_entity;

    bool operator==(const RowDigest& o) const {
        return cells == o.cells;
    }
};

struct RowDigestHash {
    size_t operator()(const RowDigest& d) const noexcept {
        size_t h = 0;
        for (uint64_t c : d.cells) {
            h ^= std::hash<uint64_t>{}(c) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

/// Per-column digest.
///
/// Entities are folded to their **id**, matching ValueHash. This makes the key
/// representation-independent: a row carrying VertexRef and a row carrying the
/// constructed VertexValue for the same vertex produce the same key, instead of
/// relying on `std::variant::operator==` (which compares the variant index
/// first and therefore reports "different" for the same entity).
///
/// Observed defect this addresses: on sf0.1,
/// `MATCH (p:Person)-[:KNOWS]-(f:Person) WITH DISTINCT f RETURN count(*)`
/// returned 2514 where `count(DISTINCT f)` -- and Neo4j -- returned 1357, i.e.
/// the two spellings of the same question disagreed and the deduplicating one
/// was wrong. Folding to id makes the deduplicating path agree with the
/// aggregate path, which is verified against Neo4j for six query shapes.
///
/// NOTE: the exact mechanism that produced 2514 was not isolated. Folding
/// entities to ids is correct and verified, but the specific representation mix
/// that triggered the disagreement in the old key does not reproduce under the
/// unit fixture below (which builds ANY-typed columns and therefore already
/// hashes both representations to the same value).
inline uint64_t cellDigest(const Value& v, uint8_t& is_entity) {
    if (std::holds_alternative<VertexRef>(v)) {
        is_entity = 1;
        return static_cast<uint64_t>(std::get<VertexRef>(v).id);
    }
    if (std::holds_alternative<VertexValue>(v)) {
        is_entity = 1;
        return static_cast<uint64_t>(std::get<VertexValue>(v).id);
    }
    if (std::holds_alternative<EdgeKey>(v)) {
        is_entity = 1;
        return static_cast<uint64_t>(std::get<EdgeKey>(v).id);
    }
    if (std::holds_alternative<EdgeValue>(v)) {
        is_entity = 1;
        return static_cast<uint64_t>(std::get<EdgeValue>(v).id);
    }
    is_entity = 0;
    return static_cast<uint64_t>(ValueHash{}(v));
}

/// Snapshot one row of `chunk` into `out` (resized as needed).
inline void digestChunkRow(const DataChunk& chunk, size_t row, RowDigest& out) {
    const size_t cols = chunk.numColumns();
    out.cells.resize(cols);
    out.is_entity.resize(cols);
    for (size_t c = 0; c < cols; ++c)
        out.cells[c] = cellDigest(chunk.columns[c].getValue(row), out.is_entity[c]);
}

} // namespace eugraph
