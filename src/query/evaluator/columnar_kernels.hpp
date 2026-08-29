#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include <cstddef>
#include <cstdint>

namespace eugraph {
namespace compute {

/// Return a sort-order category for a Value, following Cypher type ordering:
///   map < node/vertex < edge < list < path < temporal < string < bool < number < NULL
inline int cypherTypeCategory(const Value& v) {
    return std::visit(
        [](const auto& x) -> int {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return 10;
            if constexpr (std::is_same_v<T, MapValue>)
                return 0;
            if constexpr (std::is_same_v<T, VertexRef> || std::is_same_v<T, VertexValue>)
                return 1;
            if constexpr (std::is_same_v<T, EdgeKey> || std::is_same_v<T, EdgeValue>)
                return 2;
            if constexpr (std::is_same_v<T, ListValue>)
                return 3;
            if constexpr (std::is_same_v<T, PathTopology> || std::is_same_v<T, PathValue>)
                return 4;
            if constexpr (std::is_same_v<T, DateTimeValue> || std::is_same_v<T, TimeValue> ||
                          std::is_same_v<T, DurationValue>)
                return 5;
            if constexpr (std::is_same_v<T, std::string>)
                return 6;
            if constexpr (std::is_same_v<T, bool>)
                return 7;
            if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double>)
                return 8;
            return 9; // unknown values sort before NULL
        },
        v);
}

// Forward-declare for recursive list comparison.
inline int cypherCompareValues(const Value& a, const Value& b);

// Helpers
inline int compareAsInt(int64_t la, int64_t lb) {
    if (la == lb)
        return 0;
    return la < lb ? -1 : 1;
}

inline int compareAsDouble(double a, double b) {
    if (a == b)
        return 0;
    if (std::isnan(a))
        return std::isnan(b) ? 0 : 1;
    if (std::isnan(b))
        return -1;
    return a < b ? -1 : 1;
}

inline int compareString(const std::string& a, const std::string& b) {
    if (a == b)
        return 0;
    return a < b ? -1 : 1;
}

inline int compareLists(const ListValue& la, const ListValue& lb) {
    size_t na = la.elements.size();
    size_t nb = lb.elements.size();
    for (size_t i = 0; i < na && i < nb; ++i) {
        int c = cypherCompareValues(la.elements[i].value, lb.elements[i].value);
        if (c != 0)
            return c;
    }
    if (na == nb)
        return 0;
    return na < nb ? -1 : 1;
}

template <typename T> inline int compareTemporal(const T& a, const T& b) {
    if (a.kind != b.kind)
        return 0;
    if (temporalLess(a, b))
        return -1;
    if (temporalLess(b, a))
        return 1;
    return 0;
}

inline int compareDuration(const DurationValue& a, const DurationValue& b) {
    int64_t totalA = a.months * 30LL * 86400LL + a.days * 86400LL + a.seconds * 1000000000LL + a.nanos;
    int64_t totalB = b.months * 30LL * 86400LL + b.days * 86400LL + b.seconds * 1000000000LL + b.nanos;
    if (totalA == totalB)
        return 0;
    return totalA < totalB ? -1 : 1;
}

/// Compare two Values according to Cypher type ordering.
/// Returns -1 (a < b), 0 (a == b), or 1 (a > b).
inline int cypherCompareValues(const Value& a, const Value& b) {
    bool aNull = std::holds_alternative<std::monostate>(a);
    bool bNull = std::holds_alternative<std::monostate>(b);
    if (aNull || bNull) {
        if (aNull && bNull)
            return 0;
        return aNull ? 1 : -1; // NULL > non-NULL
    }

    int catA = cypherTypeCategory(a);
    int catB = cypherTypeCategory(b);
    if (catA != catB)
        return catA < catB ? -1 : 1;

    return std::visit(
        [&b](const auto& la) -> int {
            using A = std::decay_t<decltype(la)>;
            if constexpr (std::is_same_v<A, int64_t>) {
                if (auto* db = std::get_if<double>(&b))
                    return compareAsDouble(static_cast<double>(la), *db);
                return compareAsInt(la, std::get<int64_t>(b));
            }
            if constexpr (std::is_same_v<A, double>) {
                if (auto* ib = std::get_if<int64_t>(&b))
                    return compareAsDouble(la, static_cast<double>(*ib));
                return compareAsDouble(la, std::get<double>(b));
            }
            if constexpr (std::is_same_v<A, std::string>)
                return compareString(la, std::get<std::string>(b));
            if constexpr (std::is_same_v<A, bool>) {
                bool lb = std::get<bool>(b);
                return la == lb ? 0 : (la ? 1 : -1);
            }
            if constexpr (std::is_same_v<A, ListValue>)
                return compareLists(la, std::get<ListValue>(b));
            if constexpr (std::is_same_v<A, DateTimeValue>)
                return compareTemporal(la, std::get<DateTimeValue>(b));
            if constexpr (std::is_same_v<A, TimeValue>)
                return compareTemporal(la, std::get<TimeValue>(b));
            if constexpr (std::is_same_v<A, DurationValue>)
                return compareDuration(la, std::get<DurationValue>(b));
            return 0;
        },
        a);
}

namespace detail {

// Shared scalar operation semantics.
/// Shared scalar operation semantics for both typed fast paths
/// (expr/typed_common.hpp) and Value-based batch fallbacks (vector/batch_ops).
struct AddOp {
    template <typename T> static T apply(T lhs, T rhs) {
        return lhs + rhs;
    }
};

struct PlusOp {
    template <typename T> static T apply(T v) {
        return v;
    }
};

struct NotOp {
    static uint8_t apply(uint8_t v) {
        return v ? 0 : 1;
    }
};

struct SubOp {
    template <typename T> static T apply(T lhs, T rhs) {
        return lhs - rhs;
    }
};

struct MulOp {
    template <typename T> static T apply(T lhs, T rhs) {
        return lhs * rhs;
    }
};

struct LtOp {
    template <typename T> static uint8_t apply(T lhs, T rhs) {
        return lhs < rhs ? 1 : 0;
    }
};

struct LeOp {
    template <typename T> static uint8_t apply(T lhs, T rhs) {
        return lhs <= rhs ? 1 : 0;
    }
};

struct GteOp {
    template <typename T> static uint8_t apply(T lhs, T rhs) {
        return lhs >= rhs ? 1 : 0;
    }
};

struct GtOp {
    template <typename T> static uint8_t apply(T lhs, T rhs) {
        return lhs > rhs ? 1 : 0;
    }
};

struct NegOp {
    template <typename T> static T apply(T v) {
        return -v;
    }
};

// Typed raw-pointer loops and representation helpers.
struct RowIndex {
    const uint32_t* sel = nullptr;
    size_t count = 0;

    size_t physical(size_t logical) const {
        return sel ? static_cast<size_t>(sel[logical]) : logical;
    }
};

inline bool allValidFlatRows(const Column& column, const RowIndex& rows) {
    if (column.form != VectorForm::FLAT || !column.buffer)
        return false;
    const auto& validity = column.buffer->validity;
    if (validity.empty())
        return true;

    if (rows.sel == nullptr) {
        const size_t full_bytes = rows.count / 8;
        for (size_t i = 0; i < full_bytes; ++i) {
            if (validity[i] != 0xFF)
                return false;
        }
        const size_t rem = rows.count % 8;
        if (rem != 0) {
            const uint8_t tail_mask = static_cast<uint8_t>((1U << rem) - 1U);
            if (full_bytes >= validity.size() || (validity[full_bytes] & tail_mask) != tail_mask)
                return false;
        }
        return true;
    }

    for (size_t i = 0; i < rows.count; ++i) {
        const size_t p = rows.sel[i];
        if (p / 8 >= validity.size() || (validity[p / 8] & (1U << (p % 8))) == 0)
            return false;
    }
    return true;
}

template <typename L, typename R, typename O, typename Op>
void binaryLoopDirect(const L* lhs, const R* rhs, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(lhs[i], rhs[i]);
}

template <typename L, typename R, typename O, typename Op>
void binaryLoopIndirect(const L* lhs, const R* rhs, const uint32_t* sel, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(lhs[sel[i]], rhs[sel[i]]);
}

template <typename L, typename R, typename O, typename Op>
void binaryLoopNullableDirect(const L* lhs, const R* rhs, const Column& lcol, const Column& rcol, Column& result,
                              O* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (lcol.isNull(i) || rcol.isNull(i)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(lhs[i], rhs[i]);
        }
    }
}

template <typename L, typename R, typename O, typename Op>
void binaryLoopNullableIndirect(const L* lhs, const R* rhs, const uint32_t* sel, const Column& lcol, const Column& rcol,
                                Column& result, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const size_t lp = sel[i];
        const size_t rp = sel[i];
        if (lcol.isNull(lp) || rcol.isNull(rp)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(lhs[lp], rhs[rp]);
        }
    }
}

template <typename T, typename Op> void unaryLoopDirect(const T* input, T* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(input[i]);
}

template <typename T, typename Op> void unaryLoopIndirect(const T* input, const uint32_t* sel, T* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(input[sel[i]]);
}

template <typename T, typename Op>
void unaryLoopNullableDirect(const T* input, const Column& in_col, Column& result, T* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (in_col.isNull(i)) {
            out[i] = T{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(input[i]);
        }
    }
}

template <typename T, typename Op>
void unaryLoopNullableIndirect(const T* input, const uint32_t* sel, const Column& in_col, Column& result, T* out,
                               size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const size_t p = sel[i];
        if (in_col.isNull(p)) {
            out[i] = T{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(input[p]);
        }
    }
}

template <typename T, typename O, typename Op> void scalarVectorDirect(T scalar, const T* rhs, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(scalar, rhs[i]);
}

template <typename T, typename O, typename Op>
void scalarVectorIndirect(T scalar, const T* rhs, const uint32_t* sel, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(scalar, rhs[sel[i]]);
}

template <typename T, typename O, typename Op> void vectorScalarDirect(const T* lhs, T scalar, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(lhs[i], scalar);
}

template <typename T, typename O, typename Op>
void vectorScalarIndirect(const T* lhs, T scalar, const uint32_t* sel, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(lhs[sel[i]], scalar);
}

template <typename T, typename O, typename Op>
void scalarVectorNullableDirect(T scalar, const T* rhs, const Column& rcol, bool scalar_null, Column& result, O* out,
                                size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (scalar_null || rcol.isNull(i)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(scalar, rhs[i]);
        }
    }
}

template <typename T, typename O, typename Op>
void scalarVectorNullableIndirect(T scalar, const T* rhs, const uint32_t* sel, const Column& rcol, bool scalar_null,
                                  Column& result, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const size_t p = sel[i];
        if (scalar_null || rcol.isNull(p)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(scalar, rhs[p]);
        }
    }
}

template <typename T, typename O, typename Op>
void vectorScalarNullableDirect(const T* lhs, T scalar, const Column& lcol, bool scalar_null, Column& result, O* out,
                                size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (scalar_null || lcol.isNull(i)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(lhs[i], scalar);
        }
    }
}

template <typename T, typename O, typename Op>
void vectorScalarNullableIndirect(const T* lhs, const uint32_t* sel, T scalar, const Column& lcol, bool scalar_null,
                                  Column& result, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const size_t p = sel[i];
        if (scalar_null || lcol.isNull(p)) {
            out[i] = O{};
            result.setNull(i);
        } else {
            out[i] = Op::apply(lhs[p], scalar);
        }
    }
}

template <typename T, typename O, typename Op> void scalarScalarLoop(T lhs, T rhs, O* out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = Op::apply(lhs, rhs);
}

template <typename T> const T* constantAs(const Column& column) {
    if (column.form != VectorForm::CONSTANT)
        return nullptr;
    return std::get_if<T>(&column.constant_value);
}

inline RowIndex chunkRows(const DataChunk& input) {
    RowIndex rows;
    rows.count = input.numRows();
    if (!input.sel.is_identity && !input.sel.indices.empty())
        rows.sel = input.sel.indices.data();
    return rows;
}

template <typename T> const T* columnDataPtr(const Column& column) {
    if (!column.buffer)
        return nullptr;
    if constexpr (std::is_same_v<T, int64_t>) {
        return reinterpret_cast<const T*>(column.buffer->int64_data.data());
    } else if constexpr (std::is_same_v<T, double>) {
        return reinterpret_cast<const T*>(column.buffer->double_data.data());
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return column.buffer->bool_data.data();
    } else {
        return nullptr;
    }
}

inline size_t dictPhysical(const Column& column, const RowIndex& rows, size_t i) {
    const size_t p = rows.physical(i);
    if (column.form == VectorForm::DICTIONARY && !column.dict_sel.is_identity)
        return column.dict_sel[p];
    return p;
}

// Generic typed evaluation for a binary operator on already-evaluated columns.
template <typename T, typename O, typename Op>
inline bool typedBinaryEval(const Column& lhs_src, const Column& rhs_src, const RowIndex& rows_in, Column& result) {
    if (result.form != VectorForm::FLAT || !result.buffer)
        return false;

    const Column& lhs = lhs_src;
    const Column& rhs = rhs_src;
    const RowIndex& rows = rows_in;
    const bool lhs_const = lhs.form == VectorForm::CONSTANT;
    const bool rhs_const = rhs.form == VectorForm::CONSTANT;
    if (!lhs_const && lhs.form != VectorForm::FLAT && lhs.form != VectorForm::DICTIONARY)
        return false;
    if (!rhs_const && rhs.form != VectorForm::FLAT && rhs.form != VectorForm::DICTIONARY)
        return false;
    if (!lhs_const && !lhs.buffer)
        return false;
    if (!rhs_const && !rhs.buffer)
        return false;

    const bool nullable = (!lhs_const && !allValidFlatRows(lhs, rows)) ||
                          (!rhs_const && !allValidFlatRows(rhs, rows)) || (lhs_const && lhs.isNull(0)) ||
                          (rhs_const && rhs.isNull(0));
    const size_t n = rows.count;
    result.reserve(n);
    O* out = nullptr;
    if constexpr (std::is_same_v<O, uint8_t>) {
        out = result.buffer->bool_data.data();
    } else if constexpr (std::is_same_v<O, int64_t>) {
        out = reinterpret_cast<O*>(result.buffer->int64_data.data());
    } else if constexpr (std::is_same_v<O, double>) {
        out = reinterpret_cast<O*>(result.buffer->double_data.data());
    } else {
        return false;
    }

    // Any DICTIONARY operand uses the dictionary-aware combined loop below;
    // FLAT/CONSTANT fast paths assume no dict_sel indirection.
    if (lhs.form == VectorForm::DICTIONARY || rhs.form == VectorForm::DICTIONARY) {
        const T* ldata = lhs_const ? nullptr : columnDataPtr<T>(lhs);
        const T* rdata = rhs_const ? nullptr : columnDataPtr<T>(rhs);
        const T* lscalar = lhs_const ? constantAs<T>(lhs) : nullptr;
        const T* rscalar = rhs_const ? constantAs<T>(rhs) : nullptr;
        if ((!lhs_const && !ldata) || (!rhs_const && !rdata) || (lhs_const && !lscalar) || (rhs_const && !rscalar))
            return false;
        for (size_t i = 0; i < n; ++i) {
            const size_t lp = lhs_const ? 0 : dictPhysical(lhs, rows, i);
            const size_t rp = rhs_const ? 0 : dictPhysical(rhs, rows, i);
            const bool lnull = lhs_const ? lhs.isNull(0) : lhs.buffer->isNull(lp);
            const bool rnull = rhs_const ? rhs.isNull(0) : rhs.buffer->isNull(rp);
            if (lnull || rnull) {
                out[i] = O{};
                result.setNull(i);
            } else {
                const T lv = lhs_const ? *lscalar : ldata[lp];
                const T rv = rhs_const ? *rscalar : rdata[rp];
                out[i] = Op::apply(lv, rv);
            }
        }
        return true;
    }

    if (lhs_const && rhs_const) {
        if (nullable) {
            for (size_t i = 0; i < n; ++i) {
                out[i] = O{};
                result.setNull(i);
            }
        } else {
            scalarScalarLoop<T, O, Op>(*constantAs<T>(lhs), *constantAs<T>(rhs), out, n);
        }
        return true;
    }
    if (lhs_const) {
        const T scalar = *constantAs<T>(lhs);
        const T* vec =
            rhs.buffer->int64_data.data() ? reinterpret_cast<const T*>(rhs.buffer->int64_data.data()) : nullptr;
        if constexpr (std::is_same_v<T, double>) {
            vec = reinterpret_cast<const T*>(rhs.buffer->double_data.data());
        }
        if (nullable && rows.sel)
            scalarVectorNullableIndirect<T, O, Op>(scalar, vec, rows.sel, rhs, lhs.isNull(0), result, out, n);
        else if (nullable)
            scalarVectorNullableDirect<T, O, Op>(scalar, vec, rhs, lhs.isNull(0), result, out, n);
        else if (rows.sel)
            scalarVectorIndirect<T, O, Op>(scalar, vec, rows.sel, out, n);
        else
            scalarVectorDirect<T, O, Op>(scalar, vec, out, n);
        return true;
    }
    if (rhs_const) {
        const T scalar = *constantAs<T>(rhs);
        const T* vec =
            lhs.buffer->int64_data.data() ? reinterpret_cast<const T*>(lhs.buffer->int64_data.data()) : nullptr;
        if constexpr (std::is_same_v<T, double>) {
            vec = reinterpret_cast<const T*>(lhs.buffer->double_data.data());
        }
        if (nullable && rows.sel)
            vectorScalarNullableIndirect<T, O, Op>(vec, rows.sel, scalar, lhs, rhs.isNull(0), result, out, n);
        else if (nullable)
            vectorScalarNullableDirect<T, O, Op>(vec, scalar, lhs, rhs.isNull(0), result, out, n);
        else if (rows.sel)
            vectorScalarIndirect<T, O, Op>(vec, scalar, rows.sel, out, n);
        else
            vectorScalarDirect<T, O, Op>(vec, scalar, out, n);
        return true;
    }

    const T* ldata = columnDataPtr<T>(lhs);
    const T* rdata = columnDataPtr<T>(rhs);
    if (!ldata || !rdata)
        return false;

    if (nullable && rows.sel)
        binaryLoopNullableIndirect<T, T, O, Op>(ldata, rdata, rows.sel, lhs, rhs, result, out, n);
    else if (nullable)
        binaryLoopNullableDirect<T, T, O, Op>(ldata, rdata, lhs, rhs, result, out, n);
    else if (rows.sel)
        binaryLoopIndirect<T, T, O, Op>(ldata, rdata, rows.sel, out, n);
    else
        binaryLoopDirect<T, T, O, Op>(ldata, rdata, out, n);
    return true;
}

// Generic typed evaluation for a unary operator on an already-evaluated column.
template <typename T, typename O, typename Op>
inline bool typedUnaryEval(const Column& src_in, const RowIndex& rows_in, Column& result) {
    if (result.form != VectorForm::FLAT || !result.buffer)
        return false;

    const Column& src = src_in;
    const RowIndex& rows = rows_in;
    if (src.form != VectorForm::FLAT && src.form != VectorForm::CONSTANT && src.form != VectorForm::DICTIONARY)
        return false;
    if (!src.buffer && src.form != VectorForm::CONSTANT)
        return false;

    const bool nullable = src.form == VectorForm::CONSTANT ? src.isNull(0) : !allValidFlatRows(src, rows);
    const size_t n = rows.count;
    result.reserve(n);
    O* out = nullptr;
    if constexpr (std::is_same_v<O, uint8_t>) {
        out = result.buffer->bool_data.data();
    } else if constexpr (std::is_same_v<O, int64_t>) {
        out = reinterpret_cast<O*>(result.buffer->int64_data.data());
    } else if constexpr (std::is_same_v<O, double>) {
        out = reinterpret_cast<O*>(result.buffer->double_data.data());
    } else {
        return false;
    }

    if (src.form == VectorForm::CONSTANT) {
        if (nullable) {
            for (size_t i = 0; i < n; ++i) {
                out[i] = O{};
                result.setNull(i);
            }
        } else {
            const T scalar = *constantAs<T>(src);
            for (size_t i = 0; i < n; ++i)
                out[i] = Op::apply(scalar);
        }
        return true;
    }

    const T* data = columnDataPtr<T>(src);
    if (!data)
        return false;

    if (src.form == VectorForm::DICTIONARY) {
        for (size_t i = 0; i < n; ++i) {
            const size_t p = dictPhysical(src, rows, i);
            if (nullable && src.buffer->isNull(p)) {
                out[i] = O{};
                result.setNull(i);
            } else {
                out[i] = Op::apply(data[p]);
            }
        }
        return true;
    }

    if (nullable && rows.sel)
        unaryLoopNullableIndirect<T, Op>(data, rows.sel, src, result, out, n);
    else if (nullable)
        unaryLoopNullableDirect<T, Op>(data, src, result, out, n);
    else if (rows.sel)
        unaryLoopIndirect<T, Op>(data, rows.sel, out, n);
    else
        unaryLoopDirect<T, Op>(data, out, n);
    return true;
}

// Generic Value-based fallback helpers.
template <typename T, typename Op>
inline void numericBinaryValueBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<T>(lv) && std::holds_alternative<T>(rv))
            result.setValue(i, Value(Op::apply(std::get<T>(lv), std::get<T>(rv))));
        else
            result.setNull(i);
    }
}

template <typename T, typename Op>
inline void numericCmpValueBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<T>(lv) && std::holds_alternative<T>(rv))
            result.setValue(i, Value(Op::apply(std::get<T>(lv), std::get<T>(rv)) != 0));
        else
            result.setNull(i);
    }
}

template <typename T, typename Op>
inline void numericNegateValueBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value ov = operand.getValue(i);
        if (std::holds_alternative<T>(ov))
            result.setValue(i, Value(Op::apply(std::get<T>(ov))));
        else
            result.setNull(i);
    }
}

// Pure batch computation kernels. Resolution lives in
// query/planner/binder/bind_binary_op.hpp / bind_unary_op.hpp.
void nullCmpBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericEqBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericNeqBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolAndBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolOrBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolXorBatch(const Column& left, const Column& right, Column& result, size_t count);
void inBatch(const Column& left, const Column& right, Column& result, size_t count);
void listConcatBatch(const Column& left, const Column& right, Column& result, size_t count);
void listLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void listGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void listLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void listGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericAddBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericSubBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericMulBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericDivBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericModBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericPowBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void genericGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64AddBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64SubBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64MulBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64DivBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64ModBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64PowBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64LtBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64GtBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64LteBatch(const Column& left, const Column& right, Column& result, size_t count);
void int64GteBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleAddBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleSubBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleMulBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleDivBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleModBatch(const Column& left, const Column& right, Column& result, size_t count);
void doublePowBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void doubleGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringConcatBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringStartsWithBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringEndsWithBatch(const Column& left, const Column& right, Column& result, size_t count);
void stringContainsBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalLtBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalGtBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalLteBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalGteBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalAddBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalSubBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalMulBatch(const Column& left, const Column& right, Column& result, size_t count);
void temporalDivBatch(const Column& left, const Column& right, Column& result, size_t count);
void boolNotBatch(const Column& operand, Column& result, size_t count);
void int64NegateBatch(const Column& operand, Column& result, size_t count);
void doubleNegateBatch(const Column& operand, Column& result, size_t count);
void isNullBatch(const Column& operand, Column& result, size_t count);
void isNotNullBatch(const Column& operand, Column& result, size_t count);

} // namespace detail
} // namespace compute
} // namespace eugraph
