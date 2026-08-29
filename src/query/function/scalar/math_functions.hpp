#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cmath>
#include <random>

namespace eugraph {
namespace function {
namespace scalar {

// --- abs ---

inline Value absImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::abs(std::get<int64_t>(arg)));
    if (std::holds_alternative<double>(arg))
        return Value(std::abs(std::get<double>(arg)));
    return Value{};
}

struct AbsIntOp {
    static int64_t apply(int64_t v) {
        return std::abs(v);
    }
};
struct AbsDoubleOp {
    static double apply(double v) {
        return std::abs(v);
    }
};

inline void absBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                       const EvalContext& /*ctx*/) {
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::INT64)
        typedUnaryBatch<int64_t, int64_t, AbsIntOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::DOUBLE)
        typedUnaryBatch<double, double, AbsDoubleOp>(in, result, count);
    else
        for (size_t i = 0; i < count; i++)
            result.setValue(i, absImpl(in.getValue(i)));
}

// --- sqrt ---

inline Value sqrtImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<double>(arg))
        return Value(std::sqrt(std::get<double>(arg)));
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::sqrt(static_cast<double>(std::get<int64_t>(arg))));
    return Value{};
}

struct SqrtOp {
    static double apply(double v) {
        return std::sqrt(v);
    }
};

inline void sqrtBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::DOUBLE)
        typedUnaryBatch<double, double, SqrtOp>(in, result, count);
    else
        for (size_t i = 0; i < count; i++)
            result.setValue(i, sqrtImpl(in.getValue(i)));
}

// --- rand ---

inline void randBatchFn(const std::vector<const Column*>& /*args*/, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    thread_local std::mt19937 gen(std::random_device{}());
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, Value(dist(gen)));
    }
}

// --- sign ---

inline Value signImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    int64_t s = 0;
    if (std::holds_alternative<int64_t>(arg)) {
        int64_t v = std::get<int64_t>(arg);
        s = (v > 0) ? 1 : (v < 0 ? -1 : 0);
    } else if (std::holds_alternative<double>(arg)) {
        double v = std::get<double>(arg);
        s = (v > 0.0) ? 1 : (v < 0.0 ? -1 : 0);
    } else {
        return Value{};
    }
    return Value{s};
}

struct SignIntOp {
    static int64_t apply(int64_t v) {
        return v > 0 ? 1 : (v < 0 ? -1 : 0);
    }
};
struct SignDoubleOp {
    static int64_t apply(double v) {
        return v > 0 ? 1 : (v < 0 ? -1 : 0);
    }
};

inline void signBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::INT64)
        typedUnaryBatch<int64_t, int64_t, SignIntOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::DOUBLE)
        typedUnaryBatch<double, int64_t, SignDoubleOp>(in, result, count);
    else
        for (size_t i = 0; i < count; i++)
            result.setValue(i, signImpl(in.getValue(i)));
}

// --- ceil ---

inline Value ceilImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<double>(arg))
        return Value(std::ceil(std::get<double>(arg)));
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::ceil(static_cast<double>(std::get<int64_t>(arg))));
    return Value{};
}

struct CeilIntOp {
    static double apply(int64_t v) {
        return std::ceil(static_cast<double>(v));
    }
};
struct CeilDoubleOp {
    static double apply(double v) {
        return std::ceil(v);
    }
};

inline void ceilBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::INT64)
        typedUnaryBatch<int64_t, double, CeilIntOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::DOUBLE)
        typedUnaryBatch<double, double, CeilDoubleOp>(in, result, count);
    else
        for (size_t i = 0; i < count; i++)
            result.setValue(i, ceilImpl(in.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
