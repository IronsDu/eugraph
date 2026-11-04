#pragma once

/// Cell-level access helpers for evaluator/operator hot loops.
///
/// `Column::getValue()` must construct a `Value` even when the caller only
/// needs to compare a scalar or copy a cell into another typed column. These
/// helpers keep the typed path and fall back to `Value` only when the column
/// form/type combination cannot be served without it.
///
/// They are intentionally free functions in the dataset layer: `data_chunk.hpp`
/// stays the storage/representation header, while consumers opt in explicitly.

#include "query/dataset/data_chunk.hpp"

#include <string_view>

namespace eugraph {

/// Borrow a STRING cell. Returns an empty view for non-STRING columns; callers
/// must check the column type before treating the result as meaningful.
inline std::string_view columnStringView(const Column& col, size_t row) {
    if (col.form == VectorForm::CONSTANT) {
        const auto* s = std::get_if<std::string>(&col.constant_value);
        return s != nullptr ? std::string_view(*s) : std::string_view{};
    }
    if (!col.buffer || col.buffer->type != binder::BoundTypeKind::STRING)
        return {};
    const size_t physical = (col.form == VectorForm::DICTIONARY) ? col.dict_sel[row] : row;
    if (physical >= col.buffer->string_data.size())
        return {};
    return col.buffer->string_data[physical];
}

/// Equality for two non-NULL cells, using typed comparison for scalar/string
/// columns and preserving the old `Value::operator==` fallback for other forms.
/// NULL cells return false; callers that need three-valued logic must handle
/// null before calling this.
inline bool columnCellEquals(const Column& lhs, size_t lhs_row, const Column& rhs, size_t rhs_row) {
    if (lhs.isNull(lhs_row) || rhs.isNull(rhs_row))
        return false;

    if (lhs.type == rhs.type) {
        switch (lhs.type) {
        case binder::BoundTypeKind::BOOL:
            return lhs.getBool(lhs_row) == rhs.getBool(rhs_row);
        case binder::BoundTypeKind::INT64:
            return lhs.getInt64(lhs_row) == rhs.getInt64(rhs_row);
        case binder::BoundTypeKind::DOUBLE:
            return lhs.getDouble(lhs_row) == rhs.getDouble(rhs_row);
        case binder::BoundTypeKind::STRING:
            return columnStringView(lhs, lhs_row) == columnStringView(rhs, rhs_row);
        default:
            break;
        }
    }
    return lhs.getValue(lhs_row) == rhs.getValue(rhs_row);
}

/// Compare a non-NULL cell against a runtime Value without materialising the
/// cell as a Value when the value is one of the common scalar/string kinds.
inline bool columnCellEqualsValue(const Column& col, size_t row, const Value& value) {
    if (col.isNull(row) || isNull(value))
        return false;
    if (const auto* v = std::get_if<int64_t>(&value)) {
        if (col.type == binder::BoundTypeKind::INT64)
            return col.getInt64(row) == *v;
        if (col.type == binder::BoundTypeKind::DOUBLE)
            return col.getDouble(row) == static_cast<double>(*v);
        return col.getValue(row) == value;
    }
    if (const auto* v = std::get_if<double>(&value)) {
        if (col.type == binder::BoundTypeKind::DOUBLE)
            return col.getDouble(row) == *v;
        if (col.type == binder::BoundTypeKind::INT64)
            return static_cast<double>(col.getInt64(row)) == *v;
        return col.getValue(row) == value;
    }
    if (const auto* v = std::get_if<bool>(&value)) {
        if (col.type == binder::BoundTypeKind::BOOL)
            return col.getBool(row) == *v;
        return col.getValue(row) == value;
    }
    if (const auto* v = std::get_if<std::string>(&value)) {
        if (col.type == binder::BoundTypeKind::STRING)
            return columnStringView(col, row) == *v;
        return col.getValue(row) == value;
    }
    return col.getValue(row) == value;
}

/// Copy one cell into another column, preferring the typed copy path. Returns
/// false only if the fallback itself cannot represent the value; callers may
/// ignore the bool when they only need best-effort copying.
inline bool copyColumnCell(const Column& src, size_t src_row, Column& dst, size_t dst_row) {
    if (dst.copyValueFrom(src, src_row, dst_row))
        return true;
    dst.setValue(dst_row, src.getValue(src_row));
    return true;
}

/// Resolve a vertex-id cell without materialising a Value. Handles the two
/// graph representations (VERTEX_REF / VERTEX), the integer id form used by
/// internal slots, and falls back to Value for ANY/other forms.
inline VertexId columnVertexId(const Column& col, size_t row) {
    if (col.isNull(row))
        return INVALID_VERTEX_ID;
    if (col.form == VectorForm::CONSTANT) {
        if (const auto* r = std::get_if<VertexRef>(&col.constant_value))
            return r->id;
        if (const auto* v = std::get_if<VertexValuePtr>(&col.constant_value))
            return v && *v ? (*v)->id : INVALID_VERTEX_ID;
        if (const auto* i = std::get_if<int64_t>(&col.constant_value))
            return static_cast<VertexId>(*i);
        return INVALID_VERTEX_ID;
    }
    if (!col.buffer)
        return INVALID_VERTEX_ID;
    const size_t physical = (col.form == VectorForm::DICTIONARY) ? col.dict_sel[row] : row;
    switch (col.buffer->type) {
    case binder::BoundTypeKind::VERTEX_REF:
        return physical < col.buffer->vertex_ref_data.size() ? col.buffer->vertex_ref_data[physical].id
                                                             : INVALID_VERTEX_ID;
    case binder::BoundTypeKind::VERTEX:
        if (physical < col.buffer->vertex_data.size() && col.buffer->vertex_data[physical])
            return col.buffer->vertex_data[physical]->id;
        return INVALID_VERTEX_ID;
    case binder::BoundTypeKind::INT64:
        return physical < col.buffer->int64_data.size() ? static_cast<VertexId>(col.buffer->int64_data[physical])
                                                        : INVALID_VERTEX_ID;
    default:
        break;
    }
    const Value value = col.getValue(row);
    if (const auto* r = std::get_if<VertexRef>(&value))
        return r->id;
    if (const auto* v = std::get_if<VertexValuePtr>(&value))
        return v && *v ? (*v)->id : INVALID_VERTEX_ID;
    if (const auto* i = std::get_if<int64_t>(&value))
        return static_cast<VertexId>(*i);
    return INVALID_VERTEX_ID;
}

} // namespace eugraph
