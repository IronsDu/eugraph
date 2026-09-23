#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/function/function_registry.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/binder/bind_binary_op.hpp"
#include "query/planner/binder/bind_unary_op.hpp"
#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_unary_op.hpp"
#include "query/planner/bound_type.hpp"

using namespace eugraph;
using namespace eugraph::binder;
using namespace eugraph::compute;

namespace {

BoundExpression makeBinary(cypher::BinaryOperator op, BoundTypeKind lk, BoundTypeKind rk, BoundTypeKind out_kind) {
    auto expr = std::make_unique<BoundBinaryOp>();
    expr->op = op;
    expr->left = BoundExpression(BoundColumnRef(0, BoundType(lk, nullptr), "a"));
    expr->right = BoundExpression(BoundColumnRef(1, BoundType(rk, nullptr), "b"));
    expr->result_type = BoundType(out_kind, nullptr);
    expr->fallback_fn = resolveBinaryFallbackFn(op, lk, rk);
    return BoundExpression(std::move(expr));
}

BoundExpression makeUnary(cypher::UnaryOperator op, BoundTypeKind in_kind, BoundTypeKind out_kind) {
    auto expr = std::make_unique<BoundUnaryOp>();
    expr->op = op;
    expr->operand = BoundExpression(BoundColumnRef(0, BoundType(in_kind, nullptr), "a"));
    expr->result_type = BoundType(out_kind, nullptr);
    expr->fallback_fn = resolveUnaryFallbackFn(op, in_kind);
    return BoundExpression(std::move(expr));
}

TEST(EvaluatorTypedKernelTest, Int64AddMatchesOldEvaluator) {
    constexpr size_t n = 1024;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(i * 3 + 1);
    }

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], static_cast<int64_t>(i * 4 + 1));
}

TEST(EvaluatorTypedKernelTest, Int64GreaterMatchesOldEvaluator) {
    constexpr size_t n = 1024;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(n - i);
    }

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::GT, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::BOOL), input,
        out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->bool_data[i] != 0, i > (n - i));
}

TEST(EvaluatorTypedKernelTest, DoubleAddMatchesOldEvaluator) {
    constexpr size_t n = 512;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::DOUBLE);
    input.addColumn(BoundTypeKind::DOUBLE);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->double_data[i] = static_cast<double>(i) * 0.25;
        r.buffer->double_data[i] = static_cast<double>(i) * 1.5;
    }

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::DOUBLE, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::DOUBLE, BoundTypeKind::DOUBLE, BoundTypeKind::DOUBLE),
        input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_DOUBLE_EQ(out.buffer->double_data[i], static_cast<double>(i) * 1.75);
}

TEST(EvaluatorTypedKernelTest, Int64NegateMatchesOldEvaluator) {
    constexpr size_t n = 512;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t i = 0; i < n; ++i)
        col.buffer->int64_data[i] = static_cast<int64_t>(i);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(makeUnary(cypher::UnaryOperator::NEGATE, BoundTypeKind::INT64, BoundTypeKind::INT64), input,
                       out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], -static_cast<int64_t>(i));
}

TEST(EvaluatorTypedKernelTest, NullInputFallsBackAndPropagatesNull) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(1);
    }
    l.setNull(3);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    for (size_t i = 0; i < n; ++i) {
        if (i == 3) {
            EXPECT_TRUE(out.isNull(i));
        } else {
            EXPECT_EQ(out.buffer->int64_data[i], static_cast<int64_t>(i + 1));
        }
    }
}

} // namespace

TEST(EvaluatorTypedKernelTest, NullGreaterPropagatesNull) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(2);
    }
    r.setNull(5);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::GT, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::BOOL), input,
        out);

    for (size_t i = 0; i < n; ++i) {
        if (i == 5) {
            EXPECT_TRUE(out.isNull(i));
        } else {
            EXPECT_EQ(out.buffer->bool_data[i] != 0, i > 2);
        }
    }
}

TEST(EvaluatorTypedKernelTest, NullDoubleAddPropagatesNull) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::DOUBLE);
    input.addColumn(BoundTypeKind::DOUBLE);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->double_data[i] = static_cast<double>(i);
        r.buffer->double_data[i] = 1.0;
    }
    l.setNull(2);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::DOUBLE, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::DOUBLE, BoundTypeKind::DOUBLE, BoundTypeKind::DOUBLE),
        input, out);

    for (size_t i = 0; i < n; ++i) {
        if (i == 2) {
            EXPECT_TRUE(out.isNull(i));
        } else {
            EXPECT_DOUBLE_EQ(out.buffer->double_data[i], static_cast<double>(i) + 1.0);
        }
    }
}

TEST(EvaluatorTypedKernelTest, NullNegatePropagatesNull) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t i = 0; i < n; ++i)
        col.buffer->int64_data[i] = static_cast<int64_t>(i);
    col.setNull(4);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(makeUnary(cypher::UnaryOperator::NEGATE, BoundTypeKind::INT64, BoundTypeKind::INT64), input,
                       out);

    for (size_t i = 0; i < n; ++i) {
        if (i == 4) {
            EXPECT_TRUE(out.isNull(i));
        } else {
            EXPECT_EQ(out.buffer->int64_data[i], -static_cast<int64_t>(i));
        }
    }
}

TEST(EvaluatorTypedKernelTest, SelectionVectorIndirectAdd) {
    constexpr size_t physical_n = 16;
    DataChunk input;
    input.count = physical_n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(physical_n);
    r.reserve(physical_n);
    for (size_t i = 0; i < physical_n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(i * 10);
    }
    input.sel.is_identity = false;
    input.sel.indices = {2, 5, 9};
    input.sel.count = 3;

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, input.numRows());
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    EXPECT_EQ(out.buffer->int64_data[0], 22);
    EXPECT_EQ(out.buffer->int64_data[1], 55);
    EXPECT_EQ(out.buffer->int64_data[2], 99);
}

TEST(EvaluatorTypedKernelTest, SelectionVectorWithNullFallback) {
    constexpr size_t physical_n = 16;
    DataChunk input;
    input.count = physical_n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(physical_n);
    r.reserve(physical_n);
    for (size_t i = 0; i < physical_n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(1);
    }
    l.setNull(5);
    input.sel.is_identity = false;
    input.sel.indices = {2, 5, 9};
    input.sel.count = 3;

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, input.numRows());
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    EXPECT_FALSE(out.isNull(0));
    EXPECT_EQ(out.buffer->int64_data[0], 3);
    EXPECT_TRUE(out.isNull(1));
    EXPECT_EQ(out.buffer->int64_data[2], 10);
}

TEST(EvaluatorTypedKernelTest, ConstantScalarVectorAdd) {
    constexpr size_t n = 16;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(i);
    }

    // replace left with constant
    input.columns[0] = Column::constant(Value(int64_t(5)));
    input.columns[0].type = BoundTypeKind::INT64;

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], 5 + static_cast<int64_t>(i));
}

TEST(EvaluatorTypedKernelTest, TypedIdVertexRef) {
    constexpr size_t n = 16;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::VERTEX_REF);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t i = 0; i < n; ++i)
        col.buffer->vertex_ref_data[i] = VertexRef(static_cast<VertexId>(100 + i));

    function::FunctionRegistry registry;
    registry.registerBuiltins();
    const auto* def = registry.lookup("id", {BoundType::VertexRef()});
    ASSERT_NE(def, nullptr);

    BoundFunctionCall call;
    call.func_def = def;
    call.args.push_back(BoundExpression(BoundColumnRef(0, BoundType::VertexRef(), "n")));
    call.return_type = BoundType::Int64();

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(BoundExpression(std::make_unique<BoundFunctionCall>(std::move(call))), input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], static_cast<int64_t>(100 + i));
}

TEST(EvaluatorTypedKernelTest, TypedSizeList) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::LIST);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        // reserve() leaves a handle slot empty; publish a list before filling it.
        col.buffer->list_data[i] = mk<ListValue>();
        col.buffer->list_data[i]->elements.resize(i);
    }

    function::FunctionRegistry registry;
    registry.registerBuiltins();
    const auto* def = registry.lookup("size", {BoundType::List(BoundType::Any())});
    ASSERT_NE(def, nullptr);

    BoundFunctionCall call;
    call.func_def = def;
    call.args.push_back(BoundExpression(BoundColumnRef(0, BoundType::List(BoundType::Any()), "l")));
    call.return_type = BoundType::Int64();

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(BoundExpression(std::make_unique<BoundFunctionCall>(std::move(call))), input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], static_cast<int64_t>(i));
}

TEST(EvaluatorTypedKernelTest, DictionaryColumnsMaterializedAdd) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    Column lflat = Column::flat(BoundTypeKind::INT64, n);
    Column rflat = Column::flat(BoundTypeKind::INT64, n);
    for (size_t i = 0; i < n; ++i) {
        lflat.buffer->int64_data[i] = static_cast<int64_t>(i);
        rflat.buffer->int64_data[i] = static_cast<int64_t>(10 + i);
    }
    SelectionVector sel;
    sel.is_identity = false;
    sel.indices = {3, 5, 6, 2, 7, 0, 4, 1};
    sel.count = n;
    input.columns.push_back(Column::dict(lflat.buffer, sel));
    input.columns.push_back(Column::dict(rflat.buffer, sel));

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64),
        input, out);

    EXPECT_EQ(out.buffer->int64_data[0], 16);
    EXPECT_EQ(out.buffer->int64_data[1], 20);
    EXPECT_EQ(out.buffer->int64_data[2], 22);
    EXPECT_EQ(out.buffer->int64_data[7], 12);
}

TEST(EvaluatorTypedKernelTest, NestedOperandUsesTypedUnaryAfterEvaluation) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& a = input.columns[0];
    auto& b = input.columns[1];
    a.reserve(n);
    b.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        a.buffer->int64_data[i] = static_cast<int64_t>(i);
        b.buffer->int64_data[i] = static_cast<int64_t>(1);
    }

    // -(a + b)
    BoundExpression inner =
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64);
    auto neg = std::make_unique<BoundUnaryOp>();
    neg->op = cypher::UnaryOperator::NEGATE;
    neg->operand = std::move(inner);
    neg->result_type = BoundType::Int64();
    neg->fallback_fn = resolveUnaryFallbackFn(cypher::UnaryOperator::NEGATE, BoundTypeKind::INT64);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(BoundExpression(std::move(neg)), input, out);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(out.buffer->int64_data[i], -static_cast<int64_t>(i + 1));
}

TEST(EvaluatorTypedKernelTest, MoreBinaryOperatorsTyped) {
    constexpr size_t n = 16;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = static_cast<int64_t>(i);
        r.buffer->int64_data[i] = static_cast<int64_t>(3);
    }

    auto eval = [&](cypher::BinaryOperator op, BoundTypeKind out_kind) {
        Column out = Column::flat(out_kind, n);
        ExpressionEvaluator evaluator;
        evaluator.evaluate(makeBinary(op, BoundTypeKind::INT64, BoundTypeKind::INT64, out_kind), input, out);
        return out;
    };

    Column sub = eval(cypher::BinaryOperator::SUB, BoundTypeKind::INT64);
    Column mul = eval(cypher::BinaryOperator::MUL, BoundTypeKind::INT64);
    Column lt = eval(cypher::BinaryOperator::LT, BoundTypeKind::BOOL);
    Column le = eval(cypher::BinaryOperator::LTE, BoundTypeKind::BOOL);
    Column gte = eval(cypher::BinaryOperator::GTE, BoundTypeKind::BOOL);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(sub.buffer->int64_data[i], static_cast<int64_t>(i) - 3);
        EXPECT_EQ(mul.buffer->int64_data[i], static_cast<int64_t>(i) * 3);
        EXPECT_EQ(lt.buffer->bool_data[i] != 0, i < 3);
        EXPECT_EQ(le.buffer->bool_data[i] != 0, i <= 3);
        EXPECT_EQ(gte.buffer->bool_data[i] != 0, i >= 3);
    }
}

TEST(EvaluatorTypedKernelTest, MoreUnaryOperatorsTyped) {
    constexpr size_t n = 8;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    auto& c = input.columns[0];
    c.reserve(n);
    for (size_t i = 0; i < n; ++i)
        c.buffer->int64_data[i] = static_cast<int64_t>(i);

    ExpressionEvaluator evaluator;
    Column plus = Column::flat(BoundTypeKind::INT64, n);
    evaluator.evaluate(makeUnary(cypher::UnaryOperator::PLUS, BoundTypeKind::INT64, BoundTypeKind::INT64), input, plus);
    Column notcol = Column::flat(BoundTypeKind::BOOL, n);
    input.columns[0].setNull(3);
    evaluator.evaluate(makeUnary(cypher::UnaryOperator::NOT, BoundTypeKind::BOOL, BoundTypeKind::BOOL), input, notcol);

    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(plus.buffer->int64_data[i], static_cast<int64_t>(i));
    // input column is INT64 but NOT typed path expects BOOL; use fallback semantics.
    EXPECT_TRUE(notcol.isNull(0));
}

TEST(EvaluatorTypedKernelTest, NestedTailDoesNotCrash) {
    constexpr size_t n = 1;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::LIST);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t j = 0; j < 5; ++j) {
        if (!col.buffer->list_data[0])
            col.buffer->list_data[0] = mk<ListValue>();
        col.buffer->list_data[0]->elements.push_back(ValueStorage{Value(static_cast<int64_t>(j))});
    }

    function::FunctionRegistry registry;
    registry.registerBuiltins();
    const auto* def = registry.lookup("tail", {BoundType::List(BoundType::Any())});
    ASSERT_NE(def, nullptr);

    auto makeTail = [&](BoundExpression arg) {
        auto call = std::make_unique<BoundFunctionCall>();
        call->func_def = def;
        call->args.push_back(std::move(arg));
        call->return_type = BoundType::List(BoundType::Any());
        return BoundExpression(std::move(call));
    };

    BoundExpression inner = BoundExpression(BoundColumnRef(0, BoundType::List(BoundType::Any()), "l"));
    BoundExpression outer = makeTail(makeTail(std::move(inner)));

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::LIST, n);
    evaluator.evaluate(std::move(outer), input, out);
    EXPECT_EQ(out.buffer->list_data[0]->elements.size(), 3);
}

// typedUnaryBatch 的 ListValue→ListValue 路径必须走句柄感知的读写：
// 读要解引用句柄，写要**发布新的载荷**而不是原地改（列不该改动它可能与别人共享的值）。
// 这里验证元素内容、输出与输入不共享载荷 —— 只断言 size 是抓不到这类问题的。
TEST(EvaluatorTypedKernelTest, TypedTailPublishesFreshPayloadWithSameElements) {
    constexpr size_t n = 2;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::LIST);
    auto& col = input.columns[0];
    col.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        col.buffer->list_data[i] = mk<ListValue>();
        for (int64_t j = 0; j < 4; ++j)
            col.buffer->list_data[i]->elements.push_back(ValueStorage{Value(j * 10 + static_cast<int64_t>(i))});
    }
    auto* input_payload_before = col.buffer->list_data[0].get();

    function::FunctionRegistry registry;
    registry.registerBuiltins();
    const auto* def = registry.lookup("tail", {BoundType::List(BoundType::Any())});
    ASSERT_NE(def, nullptr);

    BoundFunctionCall call;
    call.func_def = def;
    call.args.push_back(BoundExpression(BoundColumnRef(0, BoundType::List(BoundType::Any()), "l")));
    call.return_type = BoundType::List(BoundType::Any());

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::LIST, n);
    evaluator.evaluate(BoundExpression(std::make_unique<BoundFunctionCall>(std::move(call))), input, out);

    for (size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(out.buffer->list_data[i]) << "输出槽位必须已发布载荷";
        const auto& got = out.buffer->list_data[i]->elements;
        ASSERT_EQ(got.size(), 3) << "tail 去掉首元素";
        // 内容必须逐元素正确（只查 size 会漏掉"元素被搬空"这类缺陷）
        EXPECT_EQ(std::get<int64_t>(got[0].value), 10 + static_cast<int64_t>(i));
        EXPECT_EQ(std::get<int64_t>(got[1].value), 20 + static_cast<int64_t>(i));
        EXPECT_EQ(std::get<int64_t>(got[2].value), 30 + static_cast<int64_t>(i));
    }

    // 输入列不能被就地改写，也不能与输出共享同一个载荷
    EXPECT_EQ(col.buffer->list_data[0].get(), input_payload_before) << "输入句柄不应被替换";
    EXPECT_EQ(col.buffer->list_data[0]->elements.size(), 4) << "输入列表不应被改动";
    EXPECT_NE(out.buffer->list_data[0].get(), col.buffer->list_data[0].get()) << "输出必须是新载荷，不能与输入共享";
}
