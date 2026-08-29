#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include <cstddef>
#include <cstdint>

namespace eugraph {
namespace function {
namespace scalar {

namespace detail {

template <typename T> const T* columnData(const Column& column) {
    if (!column.buffer)
        return nullptr;
    if constexpr (std::is_same_v<T, int64_t>) {
        return reinterpret_cast<const T*>(column.buffer->int64_data.data());
    } else if constexpr (std::is_same_v<T, double>) {
        return reinterpret_cast<const T*>(column.buffer->double_data.data());
    } else if constexpr (std::is_same_v<T, VertexRef>) {
        return column.buffer->vertex_ref_data.data();
    } else if constexpr (std::is_same_v<T, VertexValue>) {
        return column.buffer->vertex_data.data();
    } else if constexpr (std::is_same_v<T, EdgeKey>) {
        return column.buffer->edge_key_data.data();
    } else if constexpr (std::is_same_v<T, EdgeValue>) {
        return column.buffer->edge_data.data();
    } else if constexpr (std::is_same_v<T, ListValue>) {
        return column.buffer->list_data.data();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return column.buffer->string_data.data();
    } else {
        return nullptr;
    }
}

template <typename T> T* columnOut(Column& column) {
    if (!column.buffer)
        return nullptr;
    if constexpr (std::is_same_v<T, int64_t>) {
        return reinterpret_cast<T*>(column.buffer->int64_data.data());
    } else if constexpr (std::is_same_v<T, double>) {
        return reinterpret_cast<T*>(column.buffer->double_data.data());
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return column.buffer->bool_data.data();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return column.buffer->string_data.data();
    } else if constexpr (std::is_same_v<T, ListValue>) {
        return column.buffer->list_data.data();
    } else {
        return nullptr;
    }
}

} // namespace detail

/// Typed unary batch execution for one input column.
/// Supports FLAT, CONSTANT and DICTIONARY without materializing dictionary.
template <typename In, typename Out, typename Op> void typedUnaryBatch(const Column& in, Column& result, size_t count) {
    result.reserve(count);
    Out* out = detail::columnOut<Out>(result);
    if (!out)
        return;

    if (in.form == VectorForm::CONSTANT) {
        if (in.isNull(0)) {
            for (size_t i = 0; i < count; ++i)
                result.setNull(i);
            return;
        }
        const In* scalar = std::get_if<In>(&in.constant_value);
        if (!scalar)
            return;
        for (size_t i = 0; i < count; ++i)
            out[i] = Op::apply(*scalar);
        return;
    }

    if (!in.buffer)
        return;

    const In* src = detail::columnData<In>(in);
    if (!src)
        return;

    if (in.form == VectorForm::DICTIONARY) {
        const uint32_t* sel = in.dict_sel.is_identity ? nullptr : in.dict_sel.indices.data();
        if (sel) {
            for (size_t i = 0; i < count; ++i) {
                const size_t p = sel[i];
                if (in.buffer->isNull(p))
                    result.setNull(i);
                else
                    out[i] = Op::apply(src[p]);
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                if (in.buffer->isNull(i))
                    result.setNull(i);
                else
                    out[i] = Op::apply(src[i]);
            }
        }
        return;
    }

    // FLAT
    const auto& validity = in.buffer->validity;
    bool nullable = false;
    const size_t full_bytes = count / 8;
    for (size_t i = 0; i < full_bytes; ++i) {
        if (validity[i] != 0xFF) {
            nullable = true;
            break;
        }
    }
    if (!nullable && count % 8 != 0) {
        const uint8_t tail_mask = static_cast<uint8_t>((1U << (count % 8)) - 1U);
        nullable = (validity[full_bytes] & tail_mask) != tail_mask;
    }

    if (!nullable) {
        for (size_t i = 0; i < count; ++i)
            out[i] = Op::apply(src[i]);
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (in.buffer->isNull(i))
                result.setNull(i);
            else
                out[i] = Op::apply(src[i]);
        }
    }
}

/// Typed variadic coalesce for columns of the same T.
template <typename T> void typedCoalesceBatch(const std::vector<const Column*>& args, Column& result, size_t count) {
    result.reserve(count);
    T* out = detail::columnOut<T>(result);
    if (!out)
        return;

    auto read = [&](const Column& col, size_t i) -> const T* {
        if (col.form == VectorForm::CONSTANT) {
            return col.isNull(0) ? nullptr : std::get_if<T>(&col.constant_value);
        }
        if (!col.buffer)
            return nullptr;
        const size_t p = (col.form == VectorForm::DICTIONARY && !col.dict_sel.is_identity) ? col.dict_sel[i] : i;
        return col.buffer->isNull(p) ? nullptr : detail::columnData<T>(col) + p;
    };

    for (size_t i = 0; i < count; ++i) {
        const T* found = nullptr;
        for (const auto* col : args) {
            found = read(*col, i);
            if (found)
                break;
        }
        if (found) {
            out[i] = *found;
        } else {
            result.setNull(i);
        }
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
