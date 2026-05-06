#pragma once

#include "compute_service/binder/bound_type.hpp"
#include "compute_service/executor/row.hpp"

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
struct SelectionVector {
    std::vector<uint32_t> indices; // logical_row → physical_row
    size_t count = 0;

    SelectionVector() = default;

    /// Create identity selection (all rows selected).
    static SelectionVector identity(size_t n) {
        SelectionVector sv;
        sv.indices.resize(n);
        for (size_t i = 0; i < n; ++i) {
            sv.indices[i] = static_cast<uint32_t>(i);
        }
        sv.count = n;
        return sv;
    }

    /// Filter: keep rows where predicate[row] == true.
    /// `total` is the number of physical rows in the chunk.
    void filter(const std::vector<bool>& predicate, size_t total) {
        count = 0;
        indices.resize(total);
        for (size_t i = 0; i < total; ++i) {
            if (predicate[i]) {
                indices[count++] = static_cast<uint32_t>(i);
            }
        }
    }

    uint32_t operator[](size_t i) const {
        return indices[i];
    }
    uint32_t& operator[](size_t i) {
        return indices[i];
    }
};

// ==================== Column ====================

/// A single column in a DataChunk.
///
/// Columns are typed. For concrete types (INT64, DOUBLE, BOOL, STRING, VERTEX,
/// EDGE, PATH, LIST), data is stored in a homogeneous vector + validity bitmap.
/// For ANY type, data is stored as vector<Value> (runtime polymorphic).
struct Column {
    binder::BoundTypeKind type;

    // Concrete typed data (one is active depending on type).
    std::vector<int64_t> int64_data;
    std::vector<double> double_data;
    std::vector<std::string> string_data;
    std::vector<VertexValue> vertex_data;
    std::vector<EdgeValue> edge_data;
    std::vector<PathValue> path_data;
    std::vector<ListValue> list_data;
    // Bool values stored as uint8_t vector (not vector<bool> for performance).
    std::vector<uint8_t> bool_data;

    // ANY type fallback.
    std::vector<Value> any_data;

    // Validity bitmap: byte array, bit i = row i is non-NULL.
    // Row i is NULL if (validity[i / 8] & (1 << (i % 8))) == 0.
    std::vector<uint8_t> validity;

    /// Number of allocated rows.
    size_t capacity = 0;

    Column() : type(binder::BoundTypeKind::NULL_TYPE) {}

    explicit Column(binder::BoundTypeKind t) : type(t) {}

    /// Reserve capacity for data and validity.
    void reserve(size_t n) {
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

    /// Set row i to NULL.
    void setNull(size_t i) {
        if (i / 8 >= validity.size())
            return;
        validity[i / 8] &= ~(1U << (i % 8));
    }

    /// Check if row i is NULL.
    bool isNull(size_t i) const {
        if (i / 8 >= validity.size())
            return true;
        return (validity[i / 8] & (1U << (i % 8))) == 0;
    }

    /// Set row i to valid (not NULL).
    void setValid(size_t i) {
        if (i / 8 < validity.size())
            validity[i / 8] |= (1U << (i % 8));
    }

    /// Extract the value at row i as a runtime Value.
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

    /// Set the value at row i from a runtime Value.
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

// ==================== DataChunk ====================

/// Columnar data block: each column is a homogeneously typed array.
///
/// Replaces the row-based RowBatch. Columns of the same type enable
/// SIMD-friendly operations, better cache locality, and reduced memory
/// allocation overhead. A SelectionVector marks which logical rows are
/// valid after filtering without physically reorganizing data.
struct DataChunk {
    std::vector<Column> columns;
    size_t count = 0; // logical row count (≤ capacity)

    static constexpr size_t DEFAULT_CAPACITY = 1024;

    DataChunk() = default;

    /// Add a column with the given type.
    Column& addColumn(binder::BoundTypeKind type) {
        columns.emplace_back(type);
        return columns.back();
    }

    /// Set the column types from a schema. Replaces all existing columns.
    void setSchema(const std::vector<binder::BoundType>& types) {
        columns.clear();
        columns.reserve(types.size());
        for (const auto& t : types) {
            columns.emplace_back(t.kind);
        }
    }

    /// Allocate capacity for all columns.
    void reserve(size_t n) {
        for (auto& col : columns) {
            col.reserve(n);
        }
    }

    /// Reset count to 0 (but keep allocated capacity).
    void reset() {
        count = 0;
    }

    /// Set count and ensure capacity.
    void resize(size_t n) {
        reserve(n);
        count = n;
    }

    /// Extract the value at (col_idx, row_idx) using an optional selection vector.
    Value getValue(size_t col_idx, size_t row_idx, const SelectionVector* sel = nullptr) const {
        size_t physical_row = sel ? (*sel)[row_idx] : row_idx;
        if (col_idx >= columns.size())
            return Value{};
        return columns[col_idx].getValue(physical_row);
    }

    /// Set the value at (col_idx, row_idx).
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
    std::vector<Row> toRows(const SelectionVector* sel = nullptr) const {
        std::vector<Row> rows;
        size_t n = sel ? sel->count : count;
        rows.reserve(n);
        for (size_t r = 0; r < n; ++r) {
            Row row;
            row.reserve(columns.size());
            for (size_t c = 0; c < columns.size(); ++c) {
                row.push_back(getValue(c, r, sel));
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
    size_t numRows(const SelectionVector* sel = nullptr) const {
        return sel ? sel->count : count;
    }
};

} // namespace eugraph
