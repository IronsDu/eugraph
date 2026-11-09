#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/function/function_registry.hpp"
#include "query/function/scalar/start_end_node_function.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/binder/bind_binary_op.hpp"
#include "query/planner/binder/bind_unary_op.hpp"
#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_list.hpp"
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

// ==================== startNode/endNode constant-column fast path ====================

TEST(EvaluatorTypedKernelTest, StartEndNodeConstantEdgeColumnSharesOneVertex) {
    constexpr size_t n = 32;
    auto edge = mk<EdgeValue>();
    edge->id = 7;
    edge->src_id = 101;
    edge->dst_id = 202;

    Column arg = Column::constant(Value(edge));
    Column out = Column::flat(BoundTypeKind::VERTEX, n);
    std::vector<const Column*> args{&arg};
    function::EvalContext ctx;

    function::scalar::startNodeBatchFn(args, out, n, ctx);
    for (size_t i = 0; i < n; ++i) {
        const Value v = out.getValue(i);
        ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(v)) << i;
        EXPECT_EQ(std::get<VertexValuePtr>(v)->id, 101u) << i;
    }
    const auto first = std::get<VertexValuePtr>(out.getValue(0));
    for (size_t i = 1; i < n; ++i)
        EXPECT_EQ(std::get<VertexValuePtr>(out.getValue(i)), first) << "常量边列应共享同一合成顶点";

    function::scalar::endNodeBatchFn(args, out, n, ctx);
    for (size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(out.getValue(i))) << i;
        EXPECT_EQ(std::get<VertexValuePtr>(out.getValue(i))->id, 202u) << i;
    }
}

TEST(EvaluatorTypedKernelTest, StartEndNodeFlatEdgeColumnPerRow) {
    constexpr size_t n = 8;
    Column arg = Column::flat(BoundTypeKind::EDGE, n);
    for (size_t i = 0; i < n; ++i) {
        auto edge = mk<EdgeValue>();
        edge->id = 100 + i;
        edge->src_id = 1000 + i;
        edge->dst_id = 2000 + i;
        arg.setValue(i, Value(edge));
    }
    Column out = Column::flat(BoundTypeKind::VERTEX, n);
    std::vector<const Column*> args{&arg};
    function::EvalContext ctx;

    function::scalar::startNodeBatchFn(args, out, n, ctx);
    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(std::get<VertexValuePtr>(out.getValue(i))->id, 1000u + i) << i;

    function::scalar::endNodeBatchFn(args, out, n, ctx);
    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(std::get<VertexValuePtr>(out.getValue(i))->id, 2000u + i) << i;
}

// ==================== dynamic property evaluation ====================

namespace {

function::EvalContext dynamicPropContext(std::unordered_map<LabelId, LabelDef>& defs) {
    function::EvalContext ctx;
    ctx.label_defs = &defs;
    return ctx;
}

void addNamePropertyDef(std::unordered_map<LabelId, LabelDef>& defs, LabelId label_id) {
    LabelDef def;
    def.id = label_id;
    def.name = "Person";
    PropertyDef pd;
    pd.name = "name";
    pd.type = PropertyType::STRING;
    def.properties.push_back(pd);
    defs.emplace(label_id, std::move(def));
}

BoundExpression makeDynamicPropertyRef(const std::string& property) {
    auto ref = std::make_unique<BoundDynamicPropertyRef>();
    ref->property = property;
    ref->object = BoundExpression(BoundColumnRef(0, BoundType(BoundTypeKind::VERTEX, nullptr), "n"));
    ref->result_type = BoundType::Any();
    return BoundExpression(std::move(ref));
}

} // namespace

TEST(EvaluatorTypedKernelTest, DynamicPropertyReadsOwnedProperties) {
    constexpr LabelId kLabel = 7;
    std::unordered_map<LabelId, LabelDef> defs;
    addNamePropertyDef(defs, kLabel);

    DataChunk input;
    input.count = 1;
    input.addColumn(BoundTypeKind::VERTEX);
    input.columns[0].reserve(1);
    auto vertex = mk<VertexValue>();
    vertex->id = 1;
    vertex->properties[kLabel] = Properties{PropertyValue(std::string("owned"))};
    input.columns[0].setValue(0, Value(vertex));

    ExpressionEvaluator evaluator(dynamicPropContext(defs));
    Column out = Column::flat(BoundTypeKind::STRING, 1);
    evaluator.evaluate(makeDynamicPropertyRef("name"), input, out);

    const Value v = out.getValue(0);
    ASSERT_TRUE(std::holds_alternative<std::string>(v));
    EXPECT_EQ(std::get<std::string>(v), "owned");
    // Read-only: the shared payload must not be rewritten by evaluation.
    ASSERT_EQ(vertex->properties.count(kLabel), 1u);
    ASSERT_EQ(vertex->properties.at(kLabel).size(), 1u);
}

TEST(EvaluatorTypedKernelTest, DynamicPropertyListValueStaysList) {
    constexpr LabelId kLabel = 8;
    std::unordered_map<LabelId, LabelDef> defs;
    LabelDef def;
    def.id = kLabel;
    def.name = "Person";
    PropertyDef pd;
    pd.name = "tags";
    pd.type = PropertyType::STRING_ARRAY;
    def.properties.push_back(pd);
    defs.emplace(kLabel, std::move(def));

    DataChunk input;
    input.count = 1;
    input.addColumn(BoundTypeKind::VERTEX);
    input.columns[0].reserve(1);
    auto vertex = mk<VertexValue>();
    vertex->id = 2;
    vertex->properties[kLabel] = Properties{PropertyValue(std::vector<std::string>{"a", "b"})};
    input.columns[0].setValue(0, Value(vertex));

    ExpressionEvaluator evaluator(dynamicPropContext(defs));
    Column out = Column::flat(BoundTypeKind::LIST, 1);
    evaluator.evaluate(makeDynamicPropertyRef("tags"), input, out);

    const Value v = out.getValue(0);
    ASSERT_TRUE(std::holds_alternative<ListValuePtr>(v));
    const auto& elements = std::get<ListValuePtr>(v)->elements;
    ASSERT_EQ(elements.size(), 2u);
    EXPECT_EQ(std::get<std::string>(elements[0].value), "a");
    EXPECT_EQ(std::get<std::string>(elements[1].value), "b");
}

TEST(EvaluatorTypedKernelTest, DynamicPropertyMissingReturnsNull) {
    constexpr LabelId kLabel = 9;
    std::unordered_map<LabelId, LabelDef> defs;
    addNamePropertyDef(defs, kLabel);

    DataChunk input;
    input.count = 1;
    input.addColumn(BoundTypeKind::VERTEX);
    input.columns[0].reserve(1);
    auto vertex = mk<VertexValue>();
    vertex->id = 3;
    vertex->properties[kLabel] = Properties{};
    input.columns[0].setValue(0, Value(vertex));

    ExpressionEvaluator evaluator(dynamicPropContext(defs));
    Column out = Column::flat(BoundTypeKind::ANY, 1);
    evaluator.evaluate(makeDynamicPropertyRef("name"), input, out);

    EXPECT_TRUE(std::holds_alternative<std::monostate>(out.getValue(0)));
}

TEST(EvaluatorTypedKernelTest, DynamicPropertyOnDeletedVertexThrows) {
    std::unordered_map<LabelId, LabelDef> defs;
    addNamePropertyDef(defs, 10);

    DataChunk input;
    input.count = 1;
    input.addColumn(BoundTypeKind::VERTEX);
    input.columns[0].reserve(1);
    auto vertex = mk<VertexValue>();
    vertex->id = 4;
    vertex->deleted = true;
    input.columns[0].setValue(0, Value(vertex));

    ExpressionEvaluator evaluator(dynamicPropContext(defs));
    Column out = Column::flat(BoundTypeKind::ANY, 1);
    EXPECT_THROW(evaluator.evaluate(makeDynamicPropertyRef("name"), input, out), std::runtime_error);
}

TEST(EvaluatorScratchTest, ReusesTempColumnsAcrossEvaluations) {
    constexpr size_t n = 64;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    input.columns[0].reserve(n);
    input.columns[1].reserve(n);
    for (size_t i = 0; i < n; ++i) {
        input.columns[0].buffer->int64_data[i] = static_cast<int64_t>(i);
        input.columns[1].buffer->int64_data[i] = static_cast<int64_t>(i + 1);
    }

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    auto expr =
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64);
    for (int round = 0; round < 32; ++round) {
        evaluator.evaluate(expr, input, out);
        ASSERT_EQ(out.getValue(0), Value(static_cast<int64_t>(1)));
        ASSERT_EQ(out.getValue(n - 1), Value(static_cast<int64_t>(2 * n - 1)));
    }
    // One temp column for the binary result; the loop must reuse it rather than
    // appending one per evaluation.
    EXPECT_LE(evaluator.tempColumnCapacity(), 2u);
    EXPECT_LE(evaluator.tempColumnUsed(), 1u);
}

TEST(EvaluatorScratchTest, CopyFromColumnRefPreservesNulls) {
    constexpr size_t n = 4;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.columns[0].reserve(n);
    for (size_t i = 0; i < n; ++i)
        input.columns[0].buffer->int64_data[i] = static_cast<int64_t>(i + 10);
    input.columns[0].setNull(1);

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    BoundExpression expr(BoundColumnRef(0, BoundType(BoundTypeKind::INT64, nullptr), "a"));
    evaluator.evaluate(expr, input, out);

    EXPECT_EQ(out.getValue(0), Value(static_cast<int64_t>(10)));
    EXPECT_TRUE(out.isNull(1));
    EXPECT_EQ(out.getValue(2), Value(static_cast<int64_t>(12)));
    EXPECT_EQ(out.getValue(3), Value(static_cast<int64_t>(13)));
}

TEST(EvaluatorScratchTest, PredicateBoolFastPathPreservesNulls) {
    constexpr size_t n = 4;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::BOOL);
    input.columns[0].reserve(n);
    input.columns[0].buffer->bool_data[0] = 1;
    input.columns[0].buffer->bool_data[1] = 0;
    input.columns[0].buffer->bool_data[2] = 1;
    input.columns[0].buffer->bool_data[3] = 0;
    input.columns[0].setNull(3);

    ExpressionEvaluator evaluator;
    BoundExpression expr(BoundColumnRef(0, BoundType(BoundTypeKind::BOOL, nullptr), "b"));
    std::vector<bool> out;
    evaluator.evaluatePredicate(expr, input, out);

    ASSERT_EQ(out.size(), n);
    EXPECT_TRUE(out[0]);
    EXPECT_FALSE(out[1]);
    EXPECT_TRUE(out[2]);
    EXPECT_FALSE(out[3]);
}

TEST(EvaluatorScratchTest, ListLiteralEvaluatesElementsOncePerChunk) {
    constexpr size_t n = 3;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    input.columns[0].reserve(n);
    input.columns[1].reserve(n);
    for (size_t i = 0; i < n; ++i) {
        input.columns[0].buffer->int64_data[i] = static_cast<int64_t>(i);
        input.columns[1].buffer->int64_data[i] = static_cast<int64_t>(i * 10);
    }

    auto list = std::make_unique<BoundList>();
    list->elements.push_back(BoundExpression(BoundColumnRef(0, BoundType(BoundTypeKind::INT64, nullptr), "a")));
    list->elements.push_back(BoundExpression(BoundColumnRef(1, BoundType(BoundTypeKind::INT64, nullptr), "b")));
    list->element_type = BoundType(BoundTypeKind::INT64, nullptr);
    list->result_type = BoundType::List(BoundType(BoundTypeKind::INT64, nullptr));
    BoundExpression expr(std::move(list));

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::LIST, n);
    evaluator.evaluate(expr, input, out);

    for (size_t i = 0; i < n; ++i) {
        const Value v = out.getValue(i);
        ASSERT_TRUE(std::holds_alternative<ListValuePtr>(v));
        const auto& elements = std::get<ListValuePtr>(v)->elements;
        ASSERT_EQ(elements.size(), 2u);
        EXPECT_EQ(elements[0].value, Value(static_cast<int64_t>(i)));
        EXPECT_EQ(elements[1].value, Value(static_cast<int64_t>(i * 10)));
    }
}

TEST(EvaluatorScratchTest, ResetForChunkKeepsCapacityStable) {
    constexpr size_t n = 32;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    input.columns[0].reserve(n);
    input.columns[1].reserve(n);
    for (size_t i = 0; i < n; ++i) {
        input.columns[0].buffer->int64_data[i] = static_cast<int64_t>(i);
        input.columns[1].buffer->int64_data[i] = 1;
    }

    ExpressionEvaluator evaluator;
    Column out = Column::flat(BoundTypeKind::INT64, n);
    auto expr =
        makeBinary(cypher::BinaryOperator::ADD, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::INT64);
    evaluator.evaluate(expr, input, out);
    const size_t capacity = evaluator.tempColumnCapacity();
    for (int chunk = 0; chunk < 16; ++chunk) {
        evaluator.resetForChunk();
        evaluator.evaluate(expr, input, out);
        ASSERT_EQ(out.getValue(0), Value(static_cast<int64_t>(1)));
    }
    EXPECT_EQ(evaluator.tempColumnCapacity(), capacity);
}

TEST(EvaluatorTypedKernelTest, Int64EqualityMatchesValueSemantics) {
    constexpr size_t n = 5;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::INT64);
    input.addColumn(BoundTypeKind::INT64);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    const int64_t left_vals[n] = {1, 2, 3, 4, 5};
    const int64_t right_vals[n] = {1, 0, 3, 4, 5};
    for (size_t i = 0; i < n; ++i) {
        l.buffer->int64_data[i] = left_vals[i];
        r.buffer->int64_data[i] = right_vals[i];
    }
    l.setNull(2);

    ExpressionEvaluator evaluator;
    Column eq = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::EQ, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::BOOL), input,
        eq);
    Column neq = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::NEQ, BoundTypeKind::INT64, BoundTypeKind::INT64, BoundTypeKind::BOOL), input,
        neq);

    EXPECT_FALSE(eq.isNull(0));
    EXPECT_TRUE(eq.buffer->bool_data[0] != 0);
    EXPECT_FALSE(eq.buffer->bool_data[1] != 0);
    EXPECT_TRUE(eq.isNull(2));
    EXPECT_TRUE(eq.buffer->bool_data[3] != 0);
    EXPECT_TRUE(eq.buffer->bool_data[4] != 0);
    EXPECT_FALSE(neq.buffer->bool_data[0] != 0);
    EXPECT_TRUE(neq.buffer->bool_data[1] != 0);
    EXPECT_TRUE(neq.isNull(2));
}

TEST(EvaluatorTypedKernelTest, StringEqualityHandlesConstantsAndNulls) {
    constexpr size_t n = 5;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::STRING);
    auto& l = input.columns[0];
    l.reserve(n);
    const char* values[n] = {"a", "b", "x", "d", "x"};
    for (size_t i = 0; i < n; ++i)
        l.buffer->string_data[i] = values[i];
    l.setNull(3);
    Column constant = Column::constant(Value(std::string("x")));
    constant.type = BoundTypeKind::STRING;
    input.columns.push_back(std::move(constant));

    ExpressionEvaluator evaluator;
    Column eq = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::EQ, BoundTypeKind::STRING, BoundTypeKind::STRING, BoundTypeKind::BOOL),
        input, eq);
    Column neq = Column::flat(BoundTypeKind::BOOL, n);
    evaluator.evaluate(
        makeBinary(cypher::BinaryOperator::NEQ, BoundTypeKind::STRING, BoundTypeKind::STRING, BoundTypeKind::BOOL),
        input, neq);

    EXPECT_FALSE(eq.buffer->bool_data[0] != 0);
    EXPECT_FALSE(eq.buffer->bool_data[1] != 0);
    EXPECT_TRUE(eq.buffer->bool_data[2] != 0);
    EXPECT_TRUE(eq.isNull(3));
    EXPECT_TRUE(eq.buffer->bool_data[4] != 0);
    EXPECT_TRUE(neq.buffer->bool_data[0] != 0);
    EXPECT_TRUE(neq.isNull(3));
}

TEST(EvaluatorTypedKernelTest, BoolAndOrUsesThreeValuedLogic) {
    constexpr size_t n = 9;
    DataChunk input;
    input.count = n;
    input.addColumn(BoundTypeKind::BOOL);
    input.addColumn(BoundTypeKind::BOOL);
    auto& l = input.columns[0];
    auto& r = input.columns[1];
    l.reserve(n);
    r.reserve(n);
    // true/false combos plus NULLs on either side.
    const int lv[n] = {1, 1, 0, 0, 1, -1, 0, -1, -1};
    const int rv[n] = {1, 0, 1, 0, -1, 1, -1, 0, -1};
    for (size_t i = 0; i < n; ++i) {
        if (lv[i] >= 0)
            l.buffer->bool_data[i] = lv[i] ? 1 : 0;
        else
            l.setNull(i);
        if (rv[i] >= 0)
            r.buffer->bool_data[i] = rv[i] ? 1 : 0;
        else
            r.setNull(i);
    }

    auto check = [&](cypher::BinaryOperator op, auto expected) {
        ExpressionEvaluator evaluator;
        Column out = Column::flat(BoundTypeKind::BOOL, n);
        evaluator.evaluate(makeBinary(op, BoundTypeKind::BOOL, BoundTypeKind::BOOL, BoundTypeKind::BOOL), input, out);
        for (size_t i = 0; i < n; ++i) {
            const int exp = expected[i];
            if (exp < 0) {
                EXPECT_TRUE(out.isNull(i)) << "row " << i;
            } else {
                EXPECT_FALSE(out.isNull(i)) << "row " << i;
                EXPECT_EQ(out.buffer->bool_data[i] != 0, exp != 0) << "row " << i;
            }
        }
    };
    const int and_expected[n] = {1, 0, 0, 0, -1, -1, 0, 0, -1};
    const int or_expected[n] = {1, 1, 1, 0, 1, 1, -1, -1, -1};
    const int xor_expected[n] = {0, 1, 1, 0, -1, -1, -1, -1, -1};
    check(cypher::BinaryOperator::AND, and_expected);
    check(cypher::BinaryOperator::OR, or_expected);
    check(cypher::BinaryOperator::XOR, xor_expected);
}

TEST(EdgeUniqueBatchTest, DetectsDuplicateEdgesAndIgnoresNulls) {
    function::FunctionRegistry registry;
    const auto* def = registry.lookup("__edge_unique", {});
    ASSERT_NE(def, nullptr);
    ASSERT_TRUE(static_cast<bool>(def->fn));

    constexpr size_t n = 4;
    Column a = Column::flat(BoundTypeKind::EDGE_KEY, n);
    Column b = Column::flat(BoundTypeKind::EDGE_KEY, n);
    const EdgeId a_ids[n] = {10, 10, 11, 12};
    const EdgeId b_ids[n] = {20, 10, 21, 22};
    for (size_t i = 0; i < n; ++i) {
        a.buffer->edge_key_data[i] = EdgeKey(a_ids[i], 1, 2, 3);
        b.buffer->edge_key_data[i] = EdgeKey(b_ids[i], 1, 2, 3);
    }
    b.setNull(3);

    Column out = Column::flat(BoundTypeKind::BOOL, n);
    std::vector<const Column*> args{&a, &b};
    function::EvalContext ctx;
    def->fn(args, out, n, ctx);

    EXPECT_TRUE(out.buffer->bool_data[0] != 0);  // 10, 20
    EXPECT_FALSE(out.buffer->bool_data[1] != 0); // 10, 10 -> duplicate
    EXPECT_TRUE(out.buffer->bool_data[2] != 0);  // 11, 21
    EXPECT_TRUE(out.buffer->bool_data[3] != 0);  // 12, NULL
}
