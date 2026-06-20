#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eugraph {

// ==================== Runtime Value ====================

struct VertexValue {
    VertexId id = INVALID_VERTEX_ID;
    std::unordered_map<LabelId, Properties> properties;
    std::optional<LabelIdSet> labels;
    bool deleted = false;
    bool operator==(const VertexValue& o) const {
        return id == o.id;
    }
};

struct EdgeValue {
    EdgeId id = INVALID_EDGE_ID;
    VertexId src_id = INVALID_VERTEX_ID;
    VertexId dst_id = INVALID_VERTEX_ID;
    EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
    uint64_t seq = 0;
    std::optional<Properties> properties;
    bool deleted = false;
    bool operator==(const EdgeValue& o) const {
        return id == o.id;
    }
};

struct PathValue {
    std::vector<struct ValueStorage> elements;
    bool operator==(const PathValue& o) const;
};

// Runtime value produced by expression evaluation and query execution.
// Note: std::vector<Value> cannot appear directly in the variant because Value
// would be incomplete at that point. We wrap it in a ListValue struct to break
// the recursion.
struct ListValue {
    std::vector<struct ValueStorage> elements;
    bool operator==(const ListValue& o) const;
};

struct MapValue {
    std::vector<std::pair<std::string, struct ValueStorage>> entries;
    bool operator==(const MapValue& o) const;
};

// Forward-declare the storage variant.
struct ValueStorage;

// The actual runtime value type.
// VertexId and EdgeId are both uint64_t — use VertexValue/EdgeValue to distinguish,
// or just use int64_t for IDs in the runtime Value.
//
// Topology-stage alternatives (VertexRef/EdgeKey/PathTopology) carry bare IDs
// only and are produced by Scan/Expand/VarLenExpand. Enricher operators
// upgrade them in-place to their semantic counterparts.
using Value =
    std::variant<std::monostate, bool, int64_t, double, std::string, VertexRef, EdgeKey, PathTopology, VertexValue,
                 EdgeValue, PathValue, DateTimeValue, TimeValue, DurationValue, ListValue, MapValue>;

struct ValueStorage {
    Value value;
};

// ==================== Deep equality for container types ====================

inline bool PathValue::operator==(const PathValue& o) const {
    if (elements.size() != o.elements.size())
        return false;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (!(elements[i].value == o.elements[i].value))
            return false;
    }
    return true;
}

inline bool ListValue::operator==(const ListValue& o) const {
    if (elements.size() != o.elements.size())
        return false;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (!(elements[i].value == o.elements[i].value))
            return false;
    }
    return true;
}

inline bool MapValue::operator==(const MapValue& o) const {
    if (entries.size() != o.entries.size())
        return false;
    // Order-independent comparison (Cypher map semantics)
    for (const auto& lhs_entry : entries) {
        bool found = false;
        for (const auto& rhs_entry : o.entries) {
            if (lhs_entry.first == rhs_entry.first && lhs_entry.second.value == rhs_entry.second.value) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// Helper to check if a Value is null (monostate).
inline bool isNull(const Value& v) {
    return std::holds_alternative<std::monostate>(v);
}

// ==================== Three-valued equality ====================

// Returns true (equal), false (not equal), or nullopt (unknown — a null was encountered).
// Implements Cypher comparison semantics:
//   - null == anything → null (unknown)
//   - int64/double cross-type → promote to double, compare
//   - containers → element-wise three-valued comparison
//   - different non-numeric types → false (definitively not equal)
//   - NaN == NaN → false (IEEE 754)
inline std::optional<bool> valueEquals(const Value& a, const Value& b) {
    // null == anything → unknown
    if (isNull(a) || isNull(b))
        return std::nullopt;

    // ListValue
    if (std::holds_alternative<ListValue>(a) && std::holds_alternative<ListValue>(b)) {
        const auto& la = std::get<ListValue>(a);
        const auto& lb = std::get<ListValue>(b);
        if (la.elements.size() != lb.elements.size())
            return false;
        bool hasNull = false;
        for (size_t i = 0; i < la.elements.size(); ++i) {
            auto cmp = valueEquals(la.elements[i].value, lb.elements[i].value);
            if (!cmp)
                hasNull = true;
            else if (!*cmp)
                return false;
        }
        return hasNull ? std::optional<bool>(std::nullopt) : std::optional<bool>(true);
    }

    // MapValue (order-independent key matching)
    if (std::holds_alternative<MapValue>(a) && std::holds_alternative<MapValue>(b)) {
        const auto& ma = std::get<MapValue>(a);
        const auto& mb = std::get<MapValue>(b);
        if (ma.entries.size() != mb.entries.size())
            return false;
        bool hasNull = false;
        for (const auto& lhs_entry : ma.entries) {
            bool found = false;
            for (const auto& rhs_entry : mb.entries) {
                if (lhs_entry.first == rhs_entry.first) {
                    auto cmp = valueEquals(lhs_entry.second.value, rhs_entry.second.value);
                    if (!cmp) {
                        found = true;
                        hasNull = true;
                    } else if (*cmp) {
                        found = true;
                    }
                    break;
                }
            }
            if (!found)
                return false;
        }
        return hasNull ? std::optional<bool>(std::nullopt) : std::optional<bool>(true);
    }

    // PathValue
    if (std::holds_alternative<PathValue>(a) && std::holds_alternative<PathValue>(b)) {
        const auto& pa = std::get<PathValue>(a);
        const auto& pb = std::get<PathValue>(b);
        if (pa.elements.size() != pb.elements.size())
            return false;
        bool hasNull = false;
        for (size_t i = 0; i < pa.elements.size(); ++i) {
            auto cmp = valueEquals(pa.elements[i].value, pb.elements[i].value);
            if (!cmp)
                hasNull = true;
            else if (!*cmp)
                return false;
        }
        return hasNull ? std::optional<bool>(std::nullopt) : std::optional<bool>(true);
    }

    // int64/double cross-type: promote to double
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<double>(b))
        return static_cast<double>(std::get<int64_t>(a)) == std::get<double>(b);
    if (std::holds_alternative<double>(a) && std::holds_alternative<int64_t>(b))
        return std::get<double>(a) == static_cast<double>(std::get<int64_t>(b));

    // Same variant index: use built-in comparison (bool, int64, double, string,
    // VertexValue, EdgeValue, temporal types). NaN == NaN → false per IEEE 754.
    if (a.index() == b.index())
        return a == b;

    // Different non-null, non-promotable types → definitively not equal
    return false;
}

// Three-valued ordered comparison: returns -1 (a < b), 0 (equal), 1 (a > b),
// or nullopt (unknown — null encountered or types are not ordered-comparable).
// Semantics:
//   - null in either operand → nullopt
//   - int64/double cross-type → promote to double
//   - NaN → nullopt (NaN is not ordered with anything)
//   - string vs string → lexicographic
//   - different non-numeric types → nullopt
inline std::optional<int> compareValues(const Value& a, const Value& b) {
    // null → unknown
    if (isNull(a) || isNull(b))
        return std::nullopt;

    // Check equality first (handles null, int64/double promotion, containers)
    auto eq = valueEquals(a, b);
    if (!eq)
        return std::nullopt; // null in container propagation
    if (*eq)
        return 0;

    // Not equal — determine ordering
    // int64 vs int64
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        int64_t la = std::get<int64_t>(a), lb = std::get<int64_t>(b);
        return la < lb ? -1 : 1;
    }
    // double vs double (NaN → nullopt)
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        double da = std::get<double>(a), db = std::get<double>(b);
        if (std::isnan(da) || std::isnan(db))
            return std::nullopt;
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    // int64 vs double (promote)
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<double>(b)) {
        double da = static_cast<double>(std::get<int64_t>(a)), db = std::get<double>(b);
        if (std::isnan(db))
            return std::nullopt;
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    if (std::holds_alternative<double>(a) && std::holds_alternative<int64_t>(b)) {
        double da = std::get<double>(a), db = static_cast<double>(std::get<int64_t>(b));
        if (std::isnan(da))
            return std::nullopt;
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    // string vs string
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) < std::get<std::string>(b) ? -1 : 1;
    }
    // bool vs bool (true > false)
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
        bool ba = std::get<bool>(a), bb = std::get<bool>(b);
        return ba == bb ? 0 : (ba ? 1 : -1);
    }
    // Different types → unknown for ordered comparison
    return std::nullopt;
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

    RowBatch() {
        rows.reserve(CAPACITY);
    }

    size_t size() const {
        return rows.size();
    }
    bool empty() const {
        return rows.empty();
    }
    void clear() {
        rows.clear();
    }

    void push_back(Row&& row) {
        rows.push_back(std::move(row));
    }
    void push_back(const Row& row) {
        rows.push_back(row);
    }
};

// Convenience: check if a Value holds a ListValue
inline bool isList(const Value& v) {
    return std::holds_alternative<ListValue>(v);
}

// Convenience: check if a Value holds a MapValue
inline bool isMap(const Value& v) {
    return std::holds_alternative<MapValue>(v);
}

// ==================== Hash Support ====================

struct ValueHash {
    size_t operator()(const Value& v) const noexcept {
        return std::visit(
            [](const auto& val) -> size_t {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return 0;
                } else if constexpr (std::is_same_v<T, bool>) {
                    return std::hash<bool>{}(val);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    return std::hash<int64_t>{}(val);
                } else if constexpr (std::is_same_v<T, double>) {
                    return std::hash<double>{}(val);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return std::hash<std::string>{}(val);
                } else if constexpr (std::is_same_v<T, VertexRef>) {
                    return std::hash<uint64_t>{}(val.id);
                } else if constexpr (std::is_same_v<T, EdgeKey>) {
                    size_t h = std::hash<uint64_t>{}(val.id);
                    h ^= std::hash<uint64_t>{}(val.src_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    h ^= std::hash<uint64_t>{}(val.dst_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    return h;
                } else if constexpr (std::is_same_v<T, PathTopology>) {
                    size_t h = 0;
                    for (auto v : val.vertex_ids) {
                        h ^= std::hash<uint64_t>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    }
                    for (auto e : val.edge_ids) {
                        h ^= std::hash<uint64_t>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    }
                    return h;
                } else if constexpr (std::is_same_v<T, VertexValue>) {
                    return std::hash<uint64_t>{}(val.id);
                } else if constexpr (std::is_same_v<T, EdgeValue>) {
                    return std::hash<uint64_t>{}(val.id);
                } else if constexpr (std::is_same_v<T, PathValue>) {
                    size_t h = 0;
                    for (const auto& elem : val.elements) {
                        h ^= ValueHash{}(elem.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    }
                    return h;
                } else if constexpr (std::is_same_v<T, ListValue>) {
                    size_t h = 0;
                    for (const auto& elem : val.elements) {
                        h ^= ValueHash{}(elem.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    }
                    return h;
                } else if constexpr (std::is_same_v<T, MapValue>) {
                    size_t h = 0;
                    for (const auto& [k, v] : val.entries) {
                        h ^= std::hash<std::string>{}(k) + 0x9e3779b9 + (h << 6) + (h >> 2);
                        h ^= ValueHash{}(v.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    }
                    return h;
                }
                return 0;
            },
            v);
    }
};

struct RowHash {
    size_t operator()(const Row& row) const noexcept {
        size_t h = 0;
        for (const auto& val : row) {
            h ^= ValueHash{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

struct RowEqual {
    bool operator()(const Row& a, const Row& b) const noexcept {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!(a[i] == b[i]))
                return false;
        }
        return true;
    }
};

// ==================== Execution Result ====================

struct ExecutionResult {
    Schema columns;        // column names
    std::vector<Row> rows; // data rows
    std::string error;     // error message (empty if success)
};

} // namespace eugraph
