#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace eugraph {

// ==================== Runtime Value ====================

struct VertexValue {
    VertexId id = INVALID_VERTEX_ID;
    std::optional<Properties> properties;
    std::optional<LabelIdSet> labels;
    bool operator==(const VertexValue& o) const { return id == o.id; }
};

struct EdgeValue {
    EdgeId id = INVALID_EDGE_ID;
    VertexId src_id = INVALID_VERTEX_ID;
    VertexId dst_id = INVALID_VERTEX_ID;
    EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
    std::optional<Properties> properties;
    bool operator==(const EdgeValue& o) const { return id == o.id; }
};

// Runtime value produced by expression evaluation and query execution.
// Note: std::vector<Value> cannot appear directly in the variant because Value
// would be incomplete at that point. We wrap it in a ListValue struct to break
// the recursion.
struct ListValue {
    std::vector<struct ValueStorage> elements;
    bool operator==(const ListValue& /*o*/) const { return true; /* simplified */ }
};

// Forward-declare the storage variant.
struct ValueStorage;

// The actual runtime value type.
// VertexId and EdgeId are both uint64_t — use VertexValue/EdgeValue to distinguish,
// or just use int64_t for IDs in the runtime Value.
using Value = std::variant<std::monostate, bool, int64_t, double, std::string, VertexValue,
                           EdgeValue, ListValue>;

struct ValueStorage {
    Value value;
};

// Helper to check if a Value is null (monostate).
inline bool isNull(const Value& v) {
    return std::holds_alternative<std::monostate>(v);
}

// ==================== Row ====================

// A row is positional: each column index maps to a value.
// Schema (column names) is maintained separately.
using Row = std::vector<Value>;

// Schema: column names in positional order.
using Schema = std::vector<std::string>;

// ==================== RowBatch ====================

// Batch unit for coroutine pipeline. Reduces coroutine switch overhead by ~1000x
// compared to per-row AsyncGenerator<Row>.
struct RowBatch {
    static constexpr size_t CAPACITY = 1024;

    std::vector<Row> rows;

    RowBatch() { rows.reserve(CAPACITY); }

    size_t size() const { return rows.size(); }
    bool empty() const { return rows.empty(); }
    void clear() { rows.clear(); }

    void push_back(Row&& row) { rows.push_back(std::move(row)); }
    void push_back(const Row& row) { rows.push_back(row); }
};

// Convenience: check if a Value holds a ListValue
inline bool isList(const Value& v) {
    return std::holds_alternative<ListValue>(v);
}

} // namespace eugraph
