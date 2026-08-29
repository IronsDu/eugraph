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

} // namespace scalar
} // namespace function
} // namespace eugraph
