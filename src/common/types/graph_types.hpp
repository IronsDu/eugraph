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
                 std::vector<std::string>, DateTimeValue, TimeValue, DurationValue>;

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

} // namespace eugraph
