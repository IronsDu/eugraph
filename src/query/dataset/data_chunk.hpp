#pragma once

#include "query/dataset/row.hpp"
#include "query/planner/bound_type.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eugraph {

// ==================== SelectionVector ====================

/// Marks logical rows as valid within a physical DataChunk.
/// After filtering, selected rows form a contiguous logical view
/// without physically reorganizing the chunk.
///
/// When is_identity is true, all rows are selected in order and
/// indices is not allocated.
struct SelectionVector {
    std::vector<uint32_t> indices; // logical_row → physical_row (unused when is_identity)
    size_t count = 0;
    bool is_identity = true;

    SelectionVector() = default;

    /// Create identity selection (all rows selected, zero allocation).
    static SelectionVector identity(size_t n) {
        SelectionVector sv;
        sv.count = n;
        sv.is_identity = true;
        return sv;
    }

    /// Filter: keep rows where predicate[row] == true.
    /// `total` is the number of physical rows in the chunk.
    void filter(const std::vector<bool>& predicate, size_t total) {
        count = 0;
        indices.resize(total);
        is_identity = false;
        for (size_t i = 0; i < total; ++i) {
            if (predicate[i]) {
                indices[count++] = static_cast<uint32_t>(i);
            }
        }
    }

    /// Map logical row index to physical row index.
    uint32_t operator[](size_t i) const {
        return is_identity ? static_cast<uint32_t>(i) : indices[i];
    }

    uint32_t& operator[](size_t i) {
        return indices[i];
    }

    /// Check if this is an identity selection (all rows valid in order).
    bool isIdentity() const {
        return is_identity;
    }

    /// Return the logical row count.
    size_t size() const {
        return count;
    }
};

// ==================== ColumnBuffer ====================

/// Shared data storage for a Column.
/// Multiple Columns can share the same ColumnBuffer (DICTIONARY form),
/// avoiding data copies in Filter, Skip, Limit, Expand, etc.
struct ColumnBuffer {
    binder::BoundTypeKind type = binder::BoundTypeKind::NULL_TYPE;
    size_t capacity = 0;

    std::vector<int64_t> int64_data;
    std::vector<double> double_data;
    std::vector<std::string> string_data;
    std::vector<VertexRef> vertex_ref_data;
    std::vector<EdgeKey> edge_key_data;
    std::vector<PathTopologyPtr> path_topology_data;
    // Heavy payloads are stored as shared pointers, matching the Value variant. A
    // column is a contiguous array of handles, so moving a row between operators
    // copies a pointer instead of the payload it owns.
    std::vector<VertexValuePtr> vertex_data;
    std::vector<EdgeValuePtr> edge_data;
    std::vector<PathValuePtr> path_data;
    std::vector<ListValuePtr> list_data;
    std::vector<MapValuePtr> map_data;
    std::vector<uint8_t> bool_data; // bool as uint8_t (not vector<bool>)
    std::vector<Value> any_data;

    // Validity bitmap: byte array, bit i = row i is non-NULL.
    std::vector<uint8_t> validity;

    /// Allocate capacity for the active typed vector and validity.
    void reserve(size_t n) {
        if (n <= capacity)
            return;
        capacity = n;
        validity.resize((n + 7) / 8, 0xFF); // all valid by default
        switch (type) {
        case binder::BoundTypeKind::BOOL:
            bool_data.resize(n, 0);
            break;
        case binder::BoundTypeKind::INT64:
            int64_data.resize(n, 0);
            break;
        case binder::BoundTypeKind::DOUBLE:
            double_data.resize(n, 0.0);
            break;
        case binder::BoundTypeKind::STRING:
            string_data.resize(n);
            break;
        case binder::BoundTypeKind::VERTEX_REF:
            vertex_ref_data.resize(n);
            break;
        case binder::BoundTypeKind::EDGE_KEY:
            edge_key_data.resize(n);
            break;
        case binder::BoundTypeKind::PATH_TOPOLOGY:
            path_topology_data.resize(n);
            break;
        case binder::BoundTypeKind::VERTEX:
            vertex_data.resize(n);
            break;
        case binder::BoundTypeKind::EDGE:
            edge_data.resize(n);
            break;
        case binder::BoundTypeKind::PATH:
            path_data.resize(n);
            break;
        case binder::BoundTypeKind::LIST:
            list_data.resize(n);
            break;
        case binder::BoundTypeKind::MAP:
            map_data.resize(n);
            break;
        case binder::BoundTypeKind::DATETIME:
        case binder::BoundTypeKind::TIME:
        case binder::BoundTypeKind::DURATION:
        case binder::BoundTypeKind::ANY:
        case binder::BoundTypeKind::NULL_TYPE:
            any_data.resize(n);
            break;
        }
    }

    bool isNull(size_t i) const {
        if (i / 8 >= validity.size())
            return true;
        return (validity[i / 8] & (1U << (i % 8))) == 0;
    }

    void setNull(size_t i) {
        if (i / 8 >= validity.size())
            validity.resize((i + 8) / 8, 0xFF);
        validity[i / 8] &= ~(1U << (i % 8));
    }

    void setValid(size_t i) {
        if (i / 8 < validity.size())
            validity[i / 8] |= (1U << (i % 8));
    }

    Value getValue(size_t i) const {
        if (isNull(i))
            return Value{};
        switch (type) {
        case binder::BoundTypeKind::BOOL:
            return Value(bool_data[i] != 0);
        case binder::BoundTypeKind::INT64:
            return Value(int64_data[i]);
        case binder::BoundTypeKind::DOUBLE:
            return Value(double_data[i]);
        case binder::BoundTypeKind::STRING:
            return Value(string_data[i]);
        case binder::BoundTypeKind::VERTEX_REF:
            return Value(vertex_ref_data[i]);
        case binder::BoundTypeKind::EDGE_KEY:
            return Value(edge_key_data[i]);
        case binder::BoundTypeKind::PATH_TOPOLOGY:
            return path_topology_data[i] ? Value(path_topology_data[i]) : Value{};
        // A heavy slot is a handle, and reserve() leaves it empty until something
        // publishes a payload. Treat that as null rather than handing out a Value
        // that dereferences to nothing.
        case binder::BoundTypeKind::VERTEX:
            return vertex_data[i] ? Value(vertex_data[i]) : Value{};
        case binder::BoundTypeKind::EDGE:
            return edge_data[i] ? Value(edge_data[i]) : Value{};
        case binder::BoundTypeKind::PATH:
            return path_data[i] ? Value(path_data[i]) : Value{};
        case binder::BoundTypeKind::LIST:
            return list_data[i] ? Value(list_data[i]) : Value{};
        case binder::BoundTypeKind::MAP:
            return map_data[i] ? Value(map_data[i]) : Value{};
        case binder::BoundTypeKind::DATETIME:
        case binder::BoundTypeKind::TIME:
        case binder::BoundTypeKind::DURATION:
        case binder::BoundTypeKind::ANY:
        case binder::BoundTypeKind::NULL_TYPE:
            return i < any_data.size() ? any_data[i] : Value{};
        }
        return Value{};
    }

    /// Set row i from a runtime Value.
    ///
    /// Two overloads: const& copies the active alternative out of the variant,
    /// && moves it. The move overload matters because callers often hold a Value
    /// they already own and are done with -- notably UNWIND walking a list it
    /// copied locally -- and a single const& signature forced a deep copy at the
    /// call boundary anyway. For the entity alternatives that copy is an
    /// unordered_map<LabelId, Properties> clone.
    void setValue(size_t i, const Value& val) {
        setValueImpl(i, val);
    }

    void setValue(size_t i, Value&& val) {
        setValueImpl(i, std::move(val));
    }

    /// Assign a typed payload directly, skipping the Value variant entirely.
    ///
    /// Callers that already hold the concrete type (a constructed VertexValue or
    /// EdgeValue, a resolved label set) previously had to wrap it in a Value to
    /// reach setValue, which deep-copied the payload a second time -- for an
    /// entity that means cloning its unordered_map<LabelId, Properties>. These
    /// overloads assign straight into the typed vector, leaving the single copy
    /// that storing the value inherently requires.
    void setVertexValue(size_t i, VertexValuePtr v) {
        setValid(i);
        vertex_data[i] = std::move(v);
    }

    void setEdgeValue(size_t i, EdgeValuePtr v) {
        setValid(i);
        edge_data[i] = std::move(v);
    }

private:
    template <typename V> void setValueImpl(size_t i, V&& val) {
        if (::eugraph::isNull(val)) {
            setNull(i);
            return;
        }
        setValid(i);
        switch (type) {
        case binder::BoundTypeKind::BOOL:
            if (std::holds_alternative<bool>(val))
                bool_data[i] = std::get<bool>(val) ? 1 : 0;
            break;
        case binder::BoundTypeKind::INT64:
            if (std::holds_alternative<int64_t>(val))
                int64_data[i] = std::get<int64_t>(val);
            else if (std::holds_alternative<double>(val))
                int64_data[i] = static_cast<int64_t>(std::get<double>(val));
            break;
        case binder::BoundTypeKind::DOUBLE:
            if (std::holds_alternative<double>(val))
                double_data[i] = std::get<double>(val);
            else if (std::holds_alternative<int64_t>(val))
                double_data[i] = static_cast<double>(std::get<int64_t>(val));
            break;
        case binder::BoundTypeKind::STRING:
            if (std::holds_alternative<std::string>(val))
                string_data[i] = std::get<std::string>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::VERTEX_REF:
            if (std::holds_alternative<VertexRef>(val))
                vertex_ref_data[i] = std::get<VertexRef>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::EDGE_KEY:
            if (std::holds_alternative<EdgeKey>(val))
                edge_key_data[i] = std::get<EdgeKey>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::PATH_TOPOLOGY:
            if (std::holds_alternative<PathTopologyPtr>(val))
                path_topology_data[i] = std::get<PathTopologyPtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::VERTEX:
            if (std::holds_alternative<VertexValuePtr>(val))
                vertex_data[i] = std::get<VertexValuePtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::EDGE:
            if (std::holds_alternative<EdgeValuePtr>(val))
                edge_data[i] = std::get<EdgeValuePtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::PATH:
            if (std::holds_alternative<PathValuePtr>(val))
                path_data[i] = std::get<PathValuePtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::LIST:
            if (std::holds_alternative<ListValuePtr>(val))
                list_data[i] = std::get<ListValuePtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::MAP:
            if (std::holds_alternative<MapValuePtr>(val))
                map_data[i] = std::get<MapValuePtr>(std::forward<V>(val));
            break;
        case binder::BoundTypeKind::DATETIME:
        case binder::BoundTypeKind::TIME:
        case binder::BoundTypeKind::DURATION:
        case binder::BoundTypeKind::ANY:
        case binder::BoundTypeKind::NULL_TYPE:
            if (i < any_data.size())
                any_data[i] = std::forward<V>(val);
            break;
        }
    }
};

// ==================== Column ====================

/// Physical storage form of a Column.
enum class VectorForm : uint8_t {
    FLAT,       // Owning or sharing a ColumnBuffer; logical row i = buffer[i]
    CONSTANT,   // Single value broadcast to all rows (no buffer allocated)
    DICTIONARY, // Sharing another Column's buffer; logical row i = buffer[dict_sel[i]]
};

/// A single column in a DataChunk.
///
/// Three physical forms:
/// - FLAT: standard array storage, one element per row (owns or shares buffer)
/// - CONSTANT: single shared value, all rows return the same value
/// - DICTIONARY: references another Column's buffer via shared_ptr,
///   with a SelectionVector mapping logical→physical rows.
///   Enables zero-copy filtering, slicing, and expansion.
struct Column {
    binder::BoundTypeKind type = binder::BoundTypeKind::NULL_TYPE;
    VectorForm form = VectorForm::FLAT;

    // FLAT / DICTIONARY: shared data storage
    std::shared_ptr<ColumnBuffer> buffer;

    // DICTIONARY: logical row i → physical row dict_sel[i] in buffer
    SelectionVector dict_sel;

    // CONSTANT: the shared value broadcast to all rows
    Value constant_value;

    Column() : type(binder::BoundTypeKind::NULL_TYPE), form(VectorForm::FLAT) {}

    explicit Column(binder::BoundTypeKind t) : type(t), form(VectorForm::FLAT) {}

    // ── Factories ──

    /// Create a CONSTANT column broadcasting a single value to all rows.
    static Column constant(Value val) {
        Column col;
        col.form = VectorForm::CONSTANT;
        col.constant_value = std::move(val);
        return col;
    }

    /// Create a FLAT column with an allocated buffer of the given type.
    static Column flat(binder::BoundTypeKind t, size_t capacity = 0) {
        Column col;
        col.type = t;
        col.form = VectorForm::FLAT;
        col.buffer = std::make_shared<ColumnBuffer>();
        col.buffer->type = t;
        if (capacity > 0)
            col.buffer->reserve(capacity);
        return col;
    }

    /// Create a DICTIONARY column sharing another column's buffer.
    /// Logical row i reads from buffer at physical row dict_sel[i].
    static Column dict(std::shared_ptr<ColumnBuffer> buf, SelectionVector sel) {
        Column col;
        col.type = buf->type;
        col.form = VectorForm::DICTIONARY;
        col.buffer = std::move(buf);
        col.dict_sel = std::move(sel);
        return col;
    }

    // ── Capacity ──

    /// Reserve capacity for data and validity (FLAT form only).
    void reserve(size_t n) {
        if (form == VectorForm::CONSTANT)
            return;
        if (form == VectorForm::DICTIONARY)
            return; // DICTIONARY columns are read-only, no allocation needed
        if (!buffer) {
            buffer = std::make_shared<ColumnBuffer>();
            buffer->type = type;
        }
        buffer->reserve(n);
    }

    // ── Validity ──

    /// Set row i to NULL.
    void setNull(size_t i) {
        if (form == VectorForm::CONSTANT) {
            // Mark the shared constant as null
            constant_value = Value{};
            return;
        }
        if (form == VectorForm::DICTIONARY)
            return; // DICTIONARY is read-only
        if (buffer)
            buffer->setNull(i);
    }

    /// Check if row i is NULL.
    bool isNull(size_t i) const {
        if (form == VectorForm::CONSTANT) {
            return ::eugraph::isNull(constant_value);
        }
        if (!buffer)
            return true;
        size_t physical = (form == VectorForm::DICTIONARY) ? dict_sel[i] : i;
        return buffer->isNull(physical);
    }

    /// Set row i to valid (not NULL).
    void setValid(size_t i) {
        if (form == VectorForm::CONSTANT)
            return; // CONSTANT validity is tied to the value itself
        if (form == VectorForm::DICTIONARY)
            return; // DICTIONARY is read-only
        if (buffer)
            buffer->setValid(i);
    }

    /// True when every row of this column returns the same stored value. Callers
    /// that would otherwise materialise a per-row copy can keep the broadcast form
    /// instead -- for a heavy payload that is the difference between one copy and
    /// row_count copies.
    bool isConstant() const {
        return form == VectorForm::CONSTANT;
    }

    // ── Value access ──

    /// Extract the value at logical row i as a runtime Value.
    Value getValue(size_t i) const {
        if (form == VectorForm::CONSTANT) {
            if (::eugraph::isNull(constant_value))
                return Value{};
            return constant_value;
        }
        if (!buffer)
            return Value{};
        size_t physical = (form == VectorForm::DICTIONARY) ? dict_sel[i] : i;
        return buffer->getValue(physical);
    }

    /// Set the value at logical row i from a runtime Value.
    /// Only valid for FLAT columns. DICTIONARY columns are read-only
    /// (they share data with another column).
    void setValue(size_t i, const Value& val) {
        setValueImpl(i, val);
    }

    /// Move form: avoids copying the alternative out of the variant. See
    /// ColumnBuffer::setValue for why this matters for entity alternatives.
    void setValue(size_t i, Value&& val) {
        setValueImpl(i, std::move(val));
    }

    /// Copy src's value at src_row into this column's dst_row without routing
    /// through a Value.
    ///
    /// The getValue/setValue pair deep-copies twice for a pure pass-through -- once
    /// building the variant, once assigning out of it. This copies the typed payload
    /// once. Returns false when the fast path does not apply (kind mismatch,
    /// non-FLAT forms, or out-of-range rows), leaving the caller to fall back, so
    /// behaviour is unchanged wherever it declines.
    bool copyValueFrom(const Column& src, size_t src_row, size_t dst_row) {
        if (form != VectorForm::FLAT || src.form != VectorForm::FLAT)
            return false;
        if (type != src.type || !buffer || !src.buffer)
            return false;
        if (dst_row >= buffer->capacity || src_row >= src.buffer->capacity)
            return false;
        if (src.buffer->isNull(src_row)) {
            setNull(dst_row);
            return true;
        }
        switch (type) {
#define EUGRAPH_COPY_TYPED(KIND, MEMBER)                                                                               \
    case binder::BoundTypeKind::KIND:                                                                                  \
        buffer->MEMBER##_data[dst_row] = src.buffer->MEMBER##_data[src_row];                                           \
        break;
            EUGRAPH_COPY_TYPED(INT64, int64)
            EUGRAPH_COPY_TYPED(DOUBLE, double)
            EUGRAPH_COPY_TYPED(STRING, string)
            EUGRAPH_COPY_TYPED(BOOL, bool)
            EUGRAPH_COPY_TYPED(VERTEX_REF, vertex_ref)
            EUGRAPH_COPY_TYPED(EDGE_KEY, edge_key)
            EUGRAPH_COPY_TYPED(PATH_TOPOLOGY, path_topology)
            EUGRAPH_COPY_TYPED(VERTEX, vertex)
            EUGRAPH_COPY_TYPED(EDGE, edge)
            EUGRAPH_COPY_TYPED(PATH, path)
            EUGRAPH_COPY_TYPED(LIST, list)
            EUGRAPH_COPY_TYPED(MAP, map)
#undef EUGRAPH_COPY_TYPED
        default:
            return false;
        }
        setValid(dst_row);
        return true;
    }

    /// Borrow the ListValue at logical row i without copying it.
    ///
    /// Returns nullptr when row i holds no list. The pointer aliases this column's
    /// storage, so it is valid only while the column lives and is not written to --
    /// exactly what a caller iterating the list once, immediately, needs. Use it
    /// instead of getValue() when the list is only read: getValue returns a Value by
    /// value, which deep-copies the element vector.
    ///
    /// Mutable form additionally requires sole ownership of the buffer: a FLAT
    /// column can share its ColumnBuffer (see the DICTIONARY form), and handing out
    /// a mutable reference into shared storage would let the caller move elements
    /// out from under another reader.
    const ListValue* borrowList(size_t i) const {
        return borrowListImpl(i);
    }

    ListValue* borrowList(size_t i) {
        if (form != VectorForm::FLAT || !buffer || buffer.use_count() != 1)
            return nullptr;
        return borrowListImplMut(i);
    }

    /// Typed payload assignment, skipping the Value variant. Returns false when
    /// this column cannot hold the payload (wrong kind, or a read-only DICTIONARY
    /// form), leaving the caller to fall back to the Value path. The point is to
    /// avoid the extra deep copy that wrapping in a Value costs -- for an entity
    /// that copy clones its unordered_map<LabelId, Properties>.
    /// The payload arrives as a handle: a caller that owns a freshly built value
    /// wraps it with mk<T>(...), and the column takes the reference over instead of
    /// cloning the payload.
    bool setVertexValue(size_t i, VertexValuePtr v) {
        return setTypedImpl(binder::BoundTypeKind::VERTEX, [&](ColumnBuffer& b) { b.setVertexValue(i, std::move(v)); });
    }

    bool setEdgeValue(size_t i, EdgeValuePtr v) {
        return setTypedImpl(binder::BoundTypeKind::EDGE, [&](ColumnBuffer& b) { b.setEdgeValue(i, std::move(v)); });
    }

private:
    /// Non-const twin of borrowListImpl. Kept separate rather than casting away
    /// constness so the mutable path is visible at the point it is granted; its
    /// caller has already established FLAT form and sole buffer ownership.
    ListValue* borrowListImplMut(size_t i) {
        if (type != binder::BoundTypeKind::LIST || form != VectorForm::FLAT || !buffer)
            return nullptr;
        if (i >= buffer->list_data.size() || !buffer->list_data[i])
            return nullptr;
        return buffer->list_data[i].get();
    }

    const ListValue* borrowListImpl(size_t i) const {
        if (type != binder::BoundTypeKind::LIST)
            return nullptr;
        if (form == VectorForm::CONSTANT) {
            const auto* lv = std::get_if<ListValuePtr>(&constant_value);
            return lv ? lv->get() : nullptr;
        }
        if (!buffer)
            return nullptr;
        const size_t physical = (form == VectorForm::DICTIONARY) ? dict_sel[i] : i;
        if (physical >= buffer->list_data.size() || !buffer->list_data[physical])
            return nullptr;
        return buffer->list_data[physical].get();
    }

    /// Shared plumbing for the typed setters: validate the column kind and form,
    /// make sure a buffer exists, then let the caller assign into it. The row index
    /// is deliberately not a parameter -- the caller's lambda captures it, and
    /// taking it here only to ignore it trips -Wunused-parameter.
    template <typename Assign> bool setTypedImpl(binder::BoundTypeKind expected, Assign&& assign) {
        if (type != expected || form == VectorForm::DICTIONARY)
            return false;
        if (!buffer) {
            buffer = std::make_shared<ColumnBuffer>();
            buffer->type = type;
        }
        assign(*buffer);
        return true;
    }

    template <typename V> void setValueImpl(size_t i, V&& val) {
        if (form == VectorForm::CONSTANT) {
            constant_value = std::forward<V>(val);
            return;
        }
        if (form == VectorForm::DICTIONARY)
            return; // DICTIONARY is read-only — shared data must not be mutated
        if (!buffer) {
            buffer = std::make_shared<ColumnBuffer>();
            buffer->type = type;
        }
        buffer->setValue(i, std::forward<V>(val));
    }

public:
    /// Number of allocated rows in the buffer (FLAT form only).
    size_t capacity() const {
        if (!buffer)
            return 0;
        return buffer->capacity;
    }
};

// ==================== DataChunk ====================

/// Columnar data block: each column is a homogeneously typed array.
///
/// SelectionVector marks which logical rows are valid after filtering
/// without physically reorganizing data. Each Column independently
/// supports FLAT / CONSTANT / DICTIONARY forms.
struct DataChunk {
    std::vector<Column> columns;
    size_t count = 0; // logical row count (≤ capacity for FLAT columns)

    // Per-chunk selection vector: marks which rows are part of this chunk's
    // logical view. Identity = all `count` rows are valid in order.
    SelectionVector sel;

    static constexpr size_t DEFAULT_CAPACITY = 1024;

    DataChunk() = default;

    /// Add a FLAT column with the given type.
    Column& addColumn(binder::BoundTypeKind type) {
        columns.push_back(Column::flat(type));
        return columns.back();
    }

    /// Set the column types from a schema. Replaces all existing columns.
    void setSchema(const std::vector<binder::BoundType>& types) {
        columns.clear();
        columns.reserve(types.size());
        for (const auto& t : types) {
            columns.push_back(Column::flat(t.kind));
        }
    }

    /// Replace the column at `idx` with a new Column. The new column becomes
    /// the column at that index — the column count is unchanged. Used by
    /// Enricher operators to upgrade a topology-typed column (VERTEX_REF /
    /// EDGE_KEY / PATH_TOPOLOGY) to its semantic counterpart (VERTEX / EDGE /
    /// PATH) in-place: downstream operators keep referencing the same column
    /// index, but the type and data change. The old ColumnBuffer is released
    /// once its shared_ptr refcount drops to zero (no explicit teardown).
    void replaceColumn(size_t idx, Column new_col) {
        if (idx < columns.size()) {
            columns[idx] = std::move(new_col);
        }
    }

    /// Convenience: replace the column at `idx` with a freshly allocated FLAT
    /// column of the given type. The caller is expected to populate the new
    /// column's buffer before downstream consumption.
    Column& replaceColumn(size_t idx, binder::BoundTypeKind new_type, size_t capacity = 0) {
        if (idx < columns.size()) {
            columns[idx] = Column::flat(new_type, capacity);
        }
        return columns[idx];
    }

    /// Allocate capacity for all FLAT columns.
    void reserve(size_t n) {
        for (auto& col : columns) {
            col.reserve(n);
        }
    }

    /// Reset count to 0 and selection to identity.
    void reset() {
        count = 0;
        sel = SelectionVector::identity(0);
    }

    /// Set count and ensure capacity.
    void resize(size_t n) {
        reserve(n);
        count = n;
        sel = SelectionVector::identity(n);
    }

    /// Extract the value at (col_idx, logical_row_idx).
    Value getValue(size_t col_idx, size_t row_idx) const {
        if (col_idx >= columns.size())
            return Value{};
        return columns[col_idx].getValue(row_idx);
    }

    /// Set the value at (col_idx, logical_row_idx).
    void setValue(size_t col_idx, size_t row_idx, const Value& val) {
        if (col_idx < columns.size()) {
            columns[col_idx].setValue(row_idx, val);
        }
    }

    /// Append a row from individual values.
    void appendRow(const std::vector<Value>& values) {
        for (size_t i = 0; i < values.size() && i < columns.size(); ++i) {
            columns[i].setValue(count, values[i]);
        }
        ++count;
    }

    /// Convert to legacy Row vector (for transition / RPC output).

    /// Number of columns.
    size_t numColumns() const {
        return columns.size();
    }

    /// Number of logical rows.
    size_t numRows() const {
        return sel.is_identity ? count : sel.count;
    }
};

} // namespace eugraph
