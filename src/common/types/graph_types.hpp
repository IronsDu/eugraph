#pragma once

#include "common/types/temporal_value.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace eugraph {

// ==================== ID Types ====================
using VertexId = uint64_t;
using EdgeId = uint64_t;
using TransactionId = uint64_t;

using LabelId = uint16_t;
using EdgeLabelId = uint16_t;

using LabelName = std::string;
using EdgeLabelName = std::string;

// Special invalid values
constexpr VertexId INVALID_VERTEX_ID = 0;
constexpr EdgeId INVALID_EDGE_ID = 0;
constexpr LabelId INVALID_LABEL_ID = 0;
constexpr EdgeLabelId INVALID_EDGE_LABEL_ID = 0;

// ==================== Label ID Set ====================
using LabelIdSet = std::set<LabelId>;

// ==================== Transaction Handle ====================
/// Opaque transaction handle — avoids exposing underlying engine types.
using GraphTxnHandle = void*;
static constexpr GraphTxnHandle INVALID_GRAPH_TXN = nullptr;

// ==================== Property Value Type ====================
using PropertyValue =
    std::variant<std::monostate, bool, int64_t, double, std::string, std::vector<int64_t>, std::vector<double>,
                 std::vector<std::string>, DateTimeValue, TimeValue, DurationValue, std::vector<DateTimeValue>,
                 std::vector<TimeValue>, std::vector<DurationValue>>;

// Properties indexed by prop_id
using Properties = std::vector<std::optional<PropertyValue>>;

// ==================== Direction ====================
enum class Direction : uint8_t {
    OUT = 0x00,
    IN = 0x01,
    BOTH = 0x02
};

// ==================== Vertex ====================
struct Vertex {
    VertexId id;
    Properties properties;
};

// ==================== Edge ====================
struct Edge {
    EdgeId id;
    VertexId src_id;
    VertexId dst_id;
    EdgeLabelId label_id;
    Direction direction;
    Properties properties;
};

// ==================== Property Type ====================
enum class PropertyType {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    INT64_ARRAY,
    DOUBLE_ARRAY,
    STRING_ARRAY,
    DATETIME,
    TIME,
    DURATION,
    DATETIME_ARRAY,
    TIME_ARRAY,
    DURATION_ARRAY,
    ANY
};

// ==================== Temporal Property Type Mapping ====================

inline PropertyType temporalKindToPropertyType(DateTimeKind kind) {
    switch (kind) {
    case DateTimeKind::DATE:
    case DateTimeKind::LOCAL_DATETIME:
    case DateTimeKind::DATETIME:
        return PropertyType::DATETIME;
    }
    return PropertyType::ANY;
}

inline PropertyType temporalKindToPropertyType(TimeKind kind) {
    switch (kind) {
    case TimeKind::LOCAL_TIME:
    case TimeKind::TIME:
        return PropertyType::TIME;
    }
    return PropertyType::ANY;
}

inline PropertyType temporalKindToPropertyType(const DurationValue&) {
    return PropertyType::DURATION;
}

inline PropertyType propertyValueToPropertyType(const PropertyValue& pv) {
    if (std::holds_alternative<bool>(pv))
        return PropertyType::BOOL;
    if (std::holds_alternative<int64_t>(pv))
        return PropertyType::INT64;
    if (std::holds_alternative<double>(pv))
        return PropertyType::DOUBLE;
    if (std::holds_alternative<std::string>(pv))
        return PropertyType::STRING;
    if (std::holds_alternative<DateTimeValue>(pv))
        return PropertyType::DATETIME;
    if (std::holds_alternative<TimeValue>(pv))
        return PropertyType::TIME;
    if (std::holds_alternative<DurationValue>(pv))
        return PropertyType::DURATION;
    if (std::holds_alternative<std::vector<int64_t>>(pv))
        return PropertyType::INT64_ARRAY;
    if (std::holds_alternative<std::vector<double>>(pv))
        return PropertyType::DOUBLE_ARRAY;
    if (std::holds_alternative<std::vector<std::string>>(pv))
        return PropertyType::STRING_ARRAY;
    if (std::holds_alternative<std::vector<DateTimeValue>>(pv))
        return PropertyType::DATETIME_ARRAY;
    if (std::holds_alternative<std::vector<TimeValue>>(pv))
        return PropertyType::TIME_ARRAY;
    if (std::holds_alternative<std::vector<DurationValue>>(pv))
        return PropertyType::DURATION_ARRAY;
    return PropertyType::ANY;
}

// Internal label name for unlabeled nodes (never user-visible)
constexpr std::string_view kAnonLabelName = "__anon__";

struct PropertyDef {
    uint16_t id = 0;
    std::string name;
    PropertyType type;
    bool required = false;
    std::optional<PropertyValue> default_value;
};

// ==================== Index State ====================
enum class IndexState : uint8_t {
    WRITE_ONLY = 0,
    PUBLIC = 1,
    DELETE_ONLY = 2,
    ERROR = 3,
};

// ==================== Label Definition ====================
struct LabelDef {
    LabelId id = INVALID_LABEL_ID;
    LabelName name;
    std::vector<PropertyDef> properties;

    struct IndexDef {
        std::string name;
        std::vector<uint16_t> prop_ids;
        bool unique = false;
        IndexState state = IndexState::WRITE_ONLY;
    };
    std::vector<IndexDef> indexes;
};

// ==================== Edge Label Definition ====================
struct EdgeLabelDef {
    EdgeLabelId id = INVALID_EDGE_LABEL_ID;
    EdgeLabelName name;
    std::vector<PropertyDef> properties;
    bool directed = true;
    std::vector<LabelDef::IndexDef> indexes;
};

// ==================== Edge Index Scan Entry ====================
/// Full edge information returned by edge index scans.
/// Contains both the edge_id (from the key) and adjacency info (from the stored value).
struct EdgeIndexScanEntry {
    EdgeId edge_id = INVALID_EDGE_ID;
    VertexId src_id = INVALID_VERTEX_ID;
    VertexId dst_id = INVALID_VERTEX_ID;
    uint64_t seq = 0;
    EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
};

// ==================== Topology-stage Types ====================
// Lightweight, ID-only counterparts of the semantic Value types
// (VertexValue/EdgeValue/PathValue). Used by topology-stage operators
// (Scan/Expand/VarLenExpand) and promoted to their semantic form by
// Enricher operators at the latest possible position.
//
// Design notes:
//   - VertexRef is a strong type wrapping VertexId (which is itself a
//     uint64_t typedef). The strong wrapper prevents accidental
//     confusion with other uint64_t values (EdgeId, count(*), etc.)
//     at compile time, with zero runtime overhead.
//   - EdgeKey mirrors EdgeValue's topology-only fields (no Properties,
//     no deleted flag) so Expand can avoid fetching semantic data.
//   - PathTopology stores its elements in parallel arrays rather than
//     a sequence of variant Values. This is more cache-friendly for
//     batch I/O and avoids the cost of the PathValue element variants.

struct VertexRef {
    VertexId id = INVALID_VERTEX_ID;

    VertexRef() = default;
    explicit VertexRef(VertexId vid) : id(vid) {}

    bool operator==(const VertexRef& o) const {
        return id == o.id;
    }
    bool operator!=(const VertexRef& o) const {
        return id != o.id;
    }
    bool operator<(const VertexRef& o) const {
        return id < o.id;
    }
};

static_assert(sizeof(VertexRef) == sizeof(VertexId),
              "VertexRef must be zero-overhead; sizeof must equal sizeof(VertexId)");
static_assert(std::is_trivially_copyable_v<VertexRef>, "VertexRef must be trivially copyable for use in plain arrays");

struct EdgeKey {
    EdgeId id = INVALID_EDGE_ID;
    VertexId src_id = INVALID_VERTEX_ID;
    VertexId dst_id = INVALID_VERTEX_ID;
    EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
    uint32_t seq = 0;

    EdgeKey() = default;
    EdgeKey(EdgeId eid, VertexId s, VertexId d, EdgeLabelId lid, uint32_t seq_)
        : id(eid), src_id(s), dst_id(d), label_id(lid), seq(seq_) {}

    bool operator==(const EdgeKey& o) const {
        return id == o.id && src_id == o.src_id && dst_id == o.dst_id && label_id == o.label_id && seq == o.seq;
    }
    bool operator!=(const EdgeKey& o) const {
        return !(*this == o);
    }
};

// Path topology: parallel arrays of vertex/edge IDs.
// For an n-hop path, vertex_ids.size() == n+1, edge_ids.size() == n,
// edge_label_ids.size() == n, seqs.size() == n.
struct PathTopology {
    std::vector<VertexId> vertex_ids;
    std::vector<EdgeId> edge_ids;
    std::vector<EdgeLabelId> edge_label_ids;
    std::vector<uint32_t> seqs;
    std::vector<VertexId> edge_src_ids;
    std::vector<VertexId> edge_dst_ids;

    size_t hopCount() const {
        return edge_ids.size();
    }
    size_t vertexCount() const {
        return vertex_ids.size();
    }
    bool empty() const {
        return vertex_ids.empty();
    }

    bool operator==(const PathTopology& o) const {
        return vertex_ids == o.vertex_ids && edge_ids == o.edge_ids && edge_label_ids == o.edge_label_ids &&
               seqs == o.seqs;
    }
    bool operator!=(const PathTopology& o) const {
        return !(*this == o);
    }
};

} // namespace eugraph
