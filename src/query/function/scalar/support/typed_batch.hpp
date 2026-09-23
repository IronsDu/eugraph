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
    } else if constexpr (std::is_same_v<T, EdgeKey>) {
        return column.buffer->edge_key_data.data();
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
    } else {
        return nullptr;
    }
}

/// Heavy payloads are held as shared pointers (see query/dataset/row.hpp), so unlike
/// the scalar kinds above they have no contiguous `T` array: 实体/列表/路径/映射/二进制
/// 都不在 columnIn/columnOut 里（那里的 T 是元素类型），而是经 handlesOf<T>() 走句柄数组。
/// the scalar kinds they have no contiguous value array to point into. The batch
/// helpers below keep the contiguous fast path for scalars and reach handle-stored
/// kinds through the buffer, one element at a time.
template <typename T>
inline constexpr bool kHandleStored =
    std::is_same_v<T, VertexValue> || std::is_same_v<T, EdgeValue> || std::is_same_v<T, PathValue> ||
    std::is_same_v<T, ListValue> || std::is_same_v<T, MapValue> || std::is_same_v<T, BytesValue>;

/// How T appears inside the Value variant: inline for scalars, a handle for the
/// heavy kinds.
template <typename T> using VariantForm = std::conditional_t<kHandleStored<T>, ValPtr<T>, T>;

/// Reach the payload whichever form it is stored in.
template <typename T> const T& derefStored(const T& v) {
    return v;
}
template <typename T> const T& derefStored(const ValPtr<T>& v) {
    return *v;
}

/// The buffer vector backing a handle-stored kind.
template <typename T> std::vector<ValPtr<T>>& handlesOf(ColumnBuffer& buffer) {
    if constexpr (std::is_same_v<T, VertexValue>) {
        return buffer.vertex_data;
    } else if constexpr (std::is_same_v<T, EdgeValue>) {
        return buffer.edge_data;
    } else if constexpr (std::is_same_v<T, PathValue>) {
        return buffer.path_data;
    } else if constexpr (std::is_same_v<T, ListValue>) {
        return buffer.list_data;
    } else {
        return buffer.map_data;
    }
}

template <typename T> const std::vector<ValPtr<T>>& handlesOf(const ColumnBuffer& buffer) {
    if constexpr (std::is_same_v<T, VertexValue>) {
        return buffer.vertex_data;
    } else if constexpr (std::is_same_v<T, EdgeValue>) {
        return buffer.edge_data;
    } else if constexpr (std::is_same_v<T, PathValue>) {
        return buffer.path_data;
    } else if constexpr (std::is_same_v<T, ListValue>) {
        return buffer.list_data;
    } else {
        return buffer.map_data;
    }
}

/// Read element p of a column's payload.
///
/// Both storage layouts spell the same `const T*`: for scalars it points into the
/// contiguous array, for handle-stored kinds at the payload the handle owns. Either
/// way it stays valid as long as the column does.
template <typename T> const T* elementPtr(const Column& column, size_t p) {
    if (!column.buffer)
        return nullptr;
    if constexpr (kHandleStored<T>) {
        const auto& vec = handlesOf<T>(*column.buffer);
        if (p >= vec.size() || !vec[p])
            return nullptr;
        return vec[p].get();
    } else {
        const T* flat = columnData<T>(column);
        return flat ? flat + p : nullptr;
    }
}

} // namespace detail

/// Typed unary batch execution for one input column.
/// Supports FLAT, CONSTANT and DICTIONARY without materializing dictionary.
template <typename In, typename Out, typename Op> void typedUnaryBatch(const Column& in, Column& result, size_t count) {
    result.reserve(count);
    if (!result.buffer)
        return;

    // Scalars stay on the contiguous path. Handle-stored kinds index the buffer.
    const In* flat_in = nullptr;
    if constexpr (!detail::kHandleStored<In>)
        flat_in = detail::columnData<In>(in);
    Out* flat_out = nullptr;
    if constexpr (!detail::kHandleStored<Out>)
        flat_out = detail::columnOut<Out>(result);

    const auto src = [&](size_t p) -> const In* {
        if constexpr (detail::kHandleStored<In>)
            return detail::elementPtr<In>(in, p);
        else
            return flat_in ? flat_in + p : nullptr;
    };
    // Writing a handle-stored kind publishes a fresh payload: a column must not
    // mutate one that another column or row may still be sharing.
    const auto dst = [&](size_t i, const Out& v) {
        if constexpr (detail::kHandleStored<Out>)
            detail::handlesOf<Out>(*result.buffer)[i] = mk<Out>(v);
        else
            flat_out[i] = v;
    };

    if (in.form == VectorForm::CONSTANT) {
        if (in.isNull(0)) {
            for (size_t i = 0; i < count; ++i)
                result.setNull(i);
            return;
        }
        // A heavy kind's constant column carries the handle, a scalar carries the
        // value; VariantForm spells both.
        const auto* held = std::get_if<detail::VariantForm<In>>(&in.constant_value);
        if (!held)
            return;
        const In& scalar = detail::derefStored(*held);
        for (size_t i = 0; i < count; ++i)
            dst(i, Op::apply(scalar));
        return;
    }

    if (!in.buffer)
        return;
    if constexpr (!detail::kHandleStored<In>) {
        if (!flat_in)
            return;
    }

    if (in.form == VectorForm::DICTIONARY) {
        const uint32_t* sel = in.dict_sel.is_identity ? nullptr : in.dict_sel.indices.data();
        if (sel) {
            for (size_t i = 0; i < count; ++i) {
                const size_t p = sel[i];
                if (in.buffer->isNull(p))
                    result.setNull(i);
                else
                    dst(i, Op::apply(*src(p)));
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                if (in.buffer->isNull(i))
                    result.setNull(i);
                else
                    dst(i, Op::apply(*src(i)));
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
            dst(i, Op::apply(*src(i)));
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (in.buffer->isNull(i))
                result.setNull(i);
            else
                dst(i, Op::apply(*src(i)));
        }
    }
}

/// Typed variadic coalesce for columns of the same T.
template <typename T> void typedCoalesceBatch(const std::vector<const Column*>& args, Column& result, size_t count) {
    result.reserve(count);
    if (!result.buffer)
        return;
    // Every current instantiation is a scalar kind; a handle-stored one would have to
    // publish fresh payloads the way typedUnaryBatch's dst does.
    static_assert(!detail::kHandleStored<T>, "typedCoalesceBatch needs a handle-aware writer for heavy kinds");
    T* out = detail::columnOut<T>(result);
    if (!out)
        return;

    auto read = [&](const Column& col, size_t i) -> const T* {
        if (col.form == VectorForm::CONSTANT) {
            if (col.isNull(0))
                return nullptr;
            const auto* held = std::get_if<detail::VariantForm<T>>(&col.constant_value);
            return held ? &detail::derefStored(*held) : nullptr;
        }
        if (!col.buffer)
            return nullptr;
        const size_t p = (col.form == VectorForm::DICTIONARY && !col.dict_sel.is_identity) ? col.dict_sel[i] : i;
        return col.buffer->isNull(p) ? nullptr : detail::elementPtr<T>(col, p);
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
