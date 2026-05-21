#pragma once

#include "query/dataset/row.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
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
    std::vector<VertexValue> vertex_data;
    std::vector<EdgeValue> edge_data;
    std::vector<PathValue> path_data;
    std::vector<ListValue> list_data;
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
        case binder::BoundTypeKind::VERTEX:
            return Value(vertex_data[i]);
        case binder::BoundTypeKind::EDGE:
            return Value(edge_data[i]);
        case binder::BoundTypeKind::PATH:
            return Value(path_data[i]);
        case binder::BoundTypeKind::LIST:
            return Value(list_data[i]);
        case binder::BoundTypeKind::ANY:
        case binder::BoundTypeKind::NULL_TYPE:
            return i < any_data.size() ? any_data[i] : Value{};
        }
        return Value{};
    }

    void setValue(size_t i, const Value& val) {
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
                string_data[i] = std::get<std::string>(val);
            break;
        case binder::BoundTypeKind::VERTEX:
            if (std::holds_alternative<VertexValue>(val))
                vertex_data[i] = std::get<VertexValue>(val);
            break;
        case binder::BoundTypeKind::EDGE:
            if (std::holds_alternative<EdgeValue>(val))
                edge_data[i] = std::get<EdgeValue>(val);
            break;
        case binder::BoundTypeKind::PATH:
            if (std::holds_alternative<PathValue>(val))
                path_data[i] = std::get<PathValue>(val);
            break;
        case binder::BoundTypeKind::LIST:
            if (std::holds_alternative<ListValue>(val))
                list_data[i] = std::get<ListValue>(val);
            break;
        case binder::BoundTypeKind::ANY:
        case binder::BoundTypeKind::NULL_TYPE:
            if (i < any_data.size())
                any_data[i] = val;
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
        if (form == VectorForm::CONSTANT) {
            constant_value = val;
            return;
        }
        if (form == VectorForm::DICTIONARY)
            return; // DICTIONARY is read-only — shared data must not be mutated
        if (!buffer) {
            buffer = std::make_shared<ColumnBuffer>();
            buffer->type = type;
        }
        buffer->setValue(i, val);
    }

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
    std::vector<Row> toRows() const {
        std::vector<Row> rows;
        size_t n = sel.is_identity ? count : sel.count;
        rows.reserve(n);
        for (size_t r = 0; r < n; ++r) {
            Row row;
            row.reserve(columns.size());
            for (size_t c = 0; c < columns.size(); ++c) {
                row.push_back(getValue(c, r));
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

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
