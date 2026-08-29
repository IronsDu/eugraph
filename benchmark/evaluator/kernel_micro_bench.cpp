// Kernel micro benchmarks for the expression evaluator.
//
// Baseline runs use the current Value/variant based ExpressionEvaluator.
// After typed kernels land, the same benchmark bodies stay unchanged so the
// numbers remain directly comparable.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/planner/binder/bind_binary_op.hpp"
#include "query/planner/binder/bind_unary_op.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_unary_op.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_type.hpp"

namespace {

using namespace eugraph;
using namespace eugraph::binder;
using namespace eugraph::compute;

struct Int64Pair {
    DataChunk input;
    Column output;

    explicit Int64Pair(size_t count, BoundTypeKind output_kind = BoundTypeKind::INT64)
        : output(Column::flat(output_kind, count)) {
        input.count = count;
        input.addColumn(BoundTypeKind::INT64);
        input.addColumn(BoundTypeKind::INT64);
        auto& l = input.columns[0];
        auto& r = input.columns[1];
        l.reserve(count);
        r.reserve(count);
        auto* ldata = l.buffer->int64_data.data();
        auto* rdata = r.buffer->int64_data.data();
        for (size_t i = 0; i < count; ++i) {
            ldata[i] = static_cast<int64_t>(i);
            rdata[i] = static_cast<int64_t>(i * 3 + 1);
        }
    }
};

struct DoublePair {
    DataChunk input;
    Column output;

    explicit DoublePair(size_t count) : output(Column::flat(BoundTypeKind::DOUBLE, count)) {
        input.count = count;
        input.addColumn(BoundTypeKind::DOUBLE);
        input.addColumn(BoundTypeKind::DOUBLE);
        auto& l = input.columns[0];
        auto& r = input.columns[1];
        l.reserve(count);
        r.reserve(count);
        auto* ldata = l.buffer->double_data.data();
        auto* rdata = r.buffer->double_data.data();
        for (size_t i = 0; i < count; ++i) {
            ldata[i] = static_cast<double>(i);
            rdata[i] = static_cast<double>(i) * 1.5;
        }
    }
};

struct Int64Single {
    DataChunk input;
    Column output;

    explicit Int64Single(size_t count) : output(Column::flat(BoundTypeKind::INT64, count)) {
        input.count = count;
        input.addColumn(BoundTypeKind::INT64);
        auto& c = input.columns[0];
        c.reserve(count);
        auto* data = c.buffer->int64_data.data();
        for (size_t i = 0; i < count; ++i)
            data[i] = static_cast<int64_t>(i);
    }
};

BoundExpression makeBinaryExpr(cypher::BinaryOperator op, BoundTypeKind lhs_kind, BoundTypeKind rhs_kind,
                               BoundTypeKind result_kind) {
    auto expr = std::make_unique<BoundBinaryOp>();
    expr->op = op;
    expr->left = BoundExpression(BoundColumnRef(0, BoundType(lhs_kind, nullptr), "a"));
    expr->right = BoundExpression(BoundColumnRef(1, BoundType(rhs_kind, nullptr), "b"));
    expr->result_type = BoundType(result_kind, nullptr);
    expr->fallback_fn = resolveBinaryFallbackFn(op, lhs_kind, rhs_kind);
    return BoundExpression(std::move(expr));
}

BoundExpression makeUnaryExpr(cypher::UnaryOperator op, BoundTypeKind operand_kind, BoundTypeKind result_kind) {
    auto expr = std::make_unique<BoundUnaryOp>();
    expr->op = op;
    expr->operand = BoundExpression(BoundColumnRef(0, BoundType(operand_kind, nullptr), "a"));
    expr->result_type = BoundType(result_kind, nullptr);
    expr->fallback_fn = resolveUnaryFallbackFn(op, operand_kind);
    return BoundExpression(std::move(expr));
}

void bmEvaluatorInt64Add(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));
    Int64Pair data(count);
    BoundExpression expr = makeBinaryExpr(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64);

    int64_t sink = 0;
    for (auto _ : state) {
        ExpressionEvaluator evaluator;
        data.output.reserve(count);
        evaluator.evaluate(expr, data.input, data.output);
        for (size_t i = 0; i < count; ++i)
            sink += data.output.buffer->int64_data[i];
    }
    benchmark::DoNotOptimize(sink);
    state.SetItemsProcessed(state.iterations() * count);
}

void bmEvaluatorInt64Greater(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));
    Int64Pair data(count, BoundTypeKind::BOOL);
    BoundExpression expr = makeBinaryExpr(cypher::BinaryOperator::GT, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::BOOL);

    int64_t sink = 0;
    for (auto _ : state) {
        ExpressionEvaluator evaluator;
        data.output.reserve(count);
        evaluator.evaluate(expr, data.input, data.output);
        for (size_t i = 0; i < count; ++i)
            sink += data.output.buffer->bool_data[i];
    }
    benchmark::DoNotOptimize(sink);
    state.SetItemsProcessed(state.iterations() * count);
}

void bmEvaluatorInt64AddIndirect(benchmark::State& state) {
    const size_t physical = static_cast<size_t>(state.range(0));
    const size_t count = physical / 2;
    Int64Pair data(physical);
    data.input.count = physical;
    data.input.sel.is_identity = false;
    data.input.sel.indices.resize(count);
    data.input.sel.count = count;
    for (size_t i = 0; i < count; ++i)
        data.input.sel.indices[i] = static_cast<uint32_t>(i * 2);
    BoundExpression expr = makeBinaryExpr(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64,
                                          BoundTypeKind::INT64);

    int64_t sink = 0;
    for (auto _ : state) {
        ExpressionEvaluator evaluator;
        data.output.reserve(count);
        evaluator.evaluate(expr, data.input, data.output);
        for (size_t i = 0; i < count; ++i)
            sink += data.output.buffer->int64_data[i];
    }
    benchmark::DoNotOptimize(sink);
    state.SetItemsProcessed(state.iterations() * count);
}

} // namespace

void bmEvaluatorDoubleAdd(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));
    DoublePair data(count);
    BoundExpression expr = makeBinaryExpr(cypher::BinaryOperator::ADD, BoundTypeKind::DOUBLE, BoundTypeKind::DOUBLE,
                                          BoundTypeKind::DOUBLE);

    double sink = 0.0;
    for (auto _ : state) {
        ExpressionEvaluator evaluator;
        data.output.reserve(count);
        evaluator.evaluate(expr, data.input, data.output);
        for (size_t i = 0; i < count; ++i)
            sink += data.output.buffer->double_data[i];
    }
    benchmark::DoNotOptimize(sink);
    state.SetItemsProcessed(state.iterations() * count);
}

void bmEvaluatorInt64Negate(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));
    Int64Single data(count);
    BoundExpression expr = makeUnaryExpr(cypher::UnaryOperator::NEGATE, BoundTypeKind::INT64, BoundTypeKind::INT64);

    int64_t sink = 0;
    for (auto _ : state) {
        ExpressionEvaluator evaluator;
        data.output.reserve(count);
        evaluator.evaluate(expr, data.input, data.output);
        for (size_t i = 0; i < count; ++i)
            sink += data.output.buffer->int64_data[i];
    }
    benchmark::DoNotOptimize(sink);
    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(bmEvaluatorInt64Add)->RangeMultiplier(8)->Range(1 << 10, 1 << 20);
BENCHMARK(bmEvaluatorInt64Greater)->RangeMultiplier(8)->Range(1 << 10, 1 << 20);
BENCHMARK(bmEvaluatorDoubleAdd)->RangeMultiplier(8)->Range(1 << 10, 1 << 20);
BENCHMARK(bmEvaluatorInt64Negate)->RangeMultiplier(8)->Range(1 << 10, 1 << 20);
BENCHMARK(bmEvaluatorInt64AddIndirect)->RangeMultiplier(8)->Range(1 << 10, 1 << 20);
