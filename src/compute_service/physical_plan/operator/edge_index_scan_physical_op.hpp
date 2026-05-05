#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

class EdgeIndexScanPhysicalOp : public PhysicalOperator {
public:
    enum class ScanMode {
        EQUALITY,
        RANGE
    };

    // Single-property constructor
    EdgeIndexScanPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var, EdgeLabelId label_id,
                            uint16_t prop_id, ScanMode mode, PropertyValue search_value,
                            std::optional<PropertyValue> range_end, IAsyncGraphDataStore& store,
                            std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs);

    // Composite constructor
    EdgeIndexScanPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var, EdgeLabelId label_id,
                            std::vector<uint16_t> prop_ids, ScanMode mode, std::vector<PropertyValue> eq_values,
                            std::optional<std::vector<PropertyValue>> range_start,
                            std::optional<std::vector<PropertyValue>> range_end, IAsyncGraphDataStore& store,
                            std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs);

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override;

private:
    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    EdgeLabelId label_id_;
    std::vector<uint16_t> prop_ids_;
    ScanMode mode_;
    std::vector<PropertyValue> eq_values_;
    std::optional<std::vector<PropertyValue>> range_start_;
    std::optional<std::vector<PropertyValue>> range_end_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;
};

} // namespace compute
} // namespace eugraph
