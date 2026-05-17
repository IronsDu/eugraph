#include "query/physical_plan/operator/alter_edge_label_physical_op.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AlterEdgeLabelPhysicalOp::executeChunk() {
    co_await meta_.addEdgeLabelProperties(label_name_, prop_defs_);

    // Reload the updated def from metadata to keep shared maps in sync
    auto updated = co_await meta_.getEdgeLabelDef(label_name_);
    if (updated.has_value()) {
        defs_[updated->id] = std::move(*updated);
    } else {
        spdlog::warn("AlterEdgeLabel: failed to reload def for '{}' after adding properties", label_name_);
    }

    auto child_gen = child_->executeChunk();
    while (auto chunk = co_await child_gen.next()) {
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
