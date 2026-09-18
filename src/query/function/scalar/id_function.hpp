#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/scalar/support/typed_batch.hpp"

namespace eugraph {
namespace function {
namespace scalar {

/// id(vertex) -> vertex.id as int64
/// id(edge)   -> edge.id as int64
inline Value idImpl(const Value& arg) {
    if (std::holds_alternative<VertexValue>(arg)) {
        return Value(static_cast<int64_t>(std::get<VertexValue>(arg).id));
    }
    if (std::holds_alternative<VertexRef>(arg)) {
        return Value(static_cast<int64_t>(std::get<VertexRef>(arg).id));
    }
    if (std::holds_alternative<EdgeValue>(arg)) {
        return Value(static_cast<int64_t>(std::get<EdgeValue>(arg).id));
    }
    if (std::holds_alternative<EdgeKey>(arg)) {
        return Value(static_cast<int64_t>(std::get<EdgeKey>(arg).id));
    }
    return Value{};
}

/// Unified scalar callback for FunctionRegistry.
struct ExtractIdOp {
    static int64_t apply(const VertexRef& v) {
        return static_cast<int64_t>(v.id);
    }
    static int64_t apply(const VertexValue& v) {
        return static_cast<int64_t>(v.id);
    }
    static int64_t apply(const EdgeKey& e) {
        return static_cast<int64_t>(e.id);
    }
    static int64_t apply(const EdgeValue& e) {
        return static_cast<int64_t>(e.id);
    }
};

/// elementId(vertex|edge) -> 元素 ID 字符串。
///
/// 与 Bolt 5 的 element_id 字段保持同一个表示（我们的 id 就是元素标识），
/// 类型是 String —— 与 neo4j 一致（neo4j 里 id() 是整数、elementId() 是字符串）。
/// 差异：neo4j 的字符串形如 "4:<db-uuid>:32"，我们没有数据库 uuid，故只给 id 本身。
inline Value elementIdImpl(const Value& arg) {
    Value id = idImpl(arg);
    if (!std::holds_alternative<int64_t>(id))
        return Value{};
    return Value(std::to_string(std::get<int64_t>(id)));
}

/// Columnar entry: extracts id for all rows using raw typed storage.
inline void idBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                      const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::VERTEX_REF)
        typedUnaryBatch<VertexRef, int64_t, ExtractIdOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::VERTEX)
        typedUnaryBatch<VertexValue, int64_t, ExtractIdOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::EDGE_KEY)
        typedUnaryBatch<EdgeKey, int64_t, ExtractIdOp>(in, result, count);
    else if (in.type == binder::BoundTypeKind::EDGE)
        typedUnaryBatch<EdgeValue, int64_t, ExtractIdOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, idImpl(in.getValue(i)));
}

/// 批量入口：逐行取 Value 再转字符串。
/// 不走 idBatchFn 的列式快速路径 —— 结果列是 STRING，往里写 int64 会与列类型不符。
inline void elementIdBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                             const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, elementIdImpl(in.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
