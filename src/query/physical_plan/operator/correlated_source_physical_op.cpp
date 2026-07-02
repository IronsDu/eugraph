#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "common/types/graph_types.hpp"

namespace eugraph {
namespace compute {

namespace {
binder::BoundTypeKind kindFromValue(const Value& v) {
    if (std::holds_alternative<VertexRef>(v))
        return binder::BoundTypeKind::VERTEX_REF;
    if (std::holds_alternative<VertexValue>(v))
        return binder::BoundTypeKind::VERTEX;
    if (std::holds_alternative<EdgeKey>(v))
        return binder::BoundTypeKind::EDGE_KEY;
    if (std::holds_alternative<EdgeValue>(v))
        return binder::BoundTypeKind::EDGE;
    if (std::holds_alternative<PathTopology>(v))
        return binder::BoundTypeKind::PATH_TOPOLOGY;
    if (std::holds_alternative<PathValue>(v))
        return binder::BoundTypeKind::PATH;
    if (std::holds_alternative<bool>(v))
        return binder::BoundTypeKind::BOOL;
    if (std::holds_alternative<int64_t>(v))
        return binder::BoundTypeKind::INT64;
    if (std::holds_alternative<double>(v))
        return binder::BoundTypeKind::DOUBLE;
    if (std::holds_alternative<std::string>(v))
        return binder::BoundTypeKind::STRING;
    if (std::holds_alternative<ListValue>(v))
        return binder::BoundTypeKind::LIST;
    if (std::holds_alternative<MapValue>(v))
        return binder::BoundTypeKind::MAP;
    return binder::BoundTypeKind::ANY;
}
} // namespace

folly::coro::AsyncGenerator<DataChunk> CorrelatedSourcePhysicalOp::executeChunk() {
    DataChunk chunk;
    const size_t n = std::min(types_.size(), values_.size());
    for (size_t i = 0; i < n; ++i) {
        // The binder types the correlated variable as VERTEX (semantic), but
        // LeftJoin passes whatever form the left column actually holds at
        // runtime — typically VertexRef from a topology-stage Scan. Infer the
        // column kind from the value so setValue doesn't silently drop it.
        auto kind = kindFromValue(values_[i]);
        auto& col = chunk.addColumn(kind);
        col.reserve(1);
        col.setValue(0, values_[i]);
    }
    chunk.count = 1;
    chunk.sel = SelectionVector::identity(1);
    co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
