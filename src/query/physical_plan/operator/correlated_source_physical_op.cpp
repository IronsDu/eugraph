#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "common/types/graph_types.hpp"

namespace eugraph {
namespace compute {

namespace {
binder::BoundTypeKind kindFromValue(const Value& v) {
    if (std::holds_alternative<VertexRef>(v))
        return binder::BoundTypeKind::VERTEX_REF;
    if (std::holds_alternative<VertexValuePtr>(v))
        return binder::BoundTypeKind::VERTEX;
    if (std::holds_alternative<EdgeKey>(v))
        return binder::BoundTypeKind::EDGE_KEY;
    if (std::holds_alternative<EdgeValuePtr>(v))
        return binder::BoundTypeKind::EDGE;
    if (std::holds_alternative<PathTopologyPtr>(v))
        return binder::BoundTypeKind::PATH_TOPOLOGY;
    if (std::holds_alternative<PathValuePtr>(v))
        return binder::BoundTypeKind::PATH;
    if (std::holds_alternative<bool>(v))
        return binder::BoundTypeKind::BOOL;
    if (std::holds_alternative<int64_t>(v))
        return binder::BoundTypeKind::INT64;
    if (std::holds_alternative<double>(v))
        return binder::BoundTypeKind::DOUBLE;
    if (std::holds_alternative<std::string>(v))
        return binder::BoundTypeKind::STRING;
    if (std::holds_alternative<ListValuePtr>(v))
        return binder::BoundTypeKind::LIST;
    if (std::holds_alternative<MapValuePtr>(v))
        return binder::BoundTypeKind::MAP;
    return binder::BoundTypeKind::ANY;
}
} // namespace

folly::coro::AsyncGenerator<DataChunk> CorrelatedSourcePhysicalOp::executeChunk() {
    DataChunk chunk;
    if (rows_.empty())
        co_return;
    const size_t n_cols = std::min(types_.size(), rows_.front().size());
    for (size_t i = 0; i < n_cols; ++i)
        chunk.addColumn(kindFromValue(rows_.front()[i]));
    chunk.reserve(rows_.size());
    for (const auto& row : rows_) {
        for (size_t i = 0; i < n_cols && i < row.size(); ++i)
            chunk.columns[i].setValue(chunk.count, row[i]);
        ++chunk.count;
    }
    chunk.sel = SelectionVector::identity(chunk.count);
    co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
