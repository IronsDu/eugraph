#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
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

class IndexScanPhysicalOp : public PhysicalOperator {
public:
    enum class ScanMode {
        EQUALITY,
        RANGE
    };

    // Single-property constructor (backward compatible)
    IndexScanPhysicalOp(std::string variable, LabelId label_id, uint16_t prop_id, ScanMode mode,
                        PropertyValue search_value, std::optional<PropertyValue> range_end,
                        std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs,
                        std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids = {});

    // Composite constructor
    IndexScanPhysicalOp(std::string variable, LabelId label_id, std::vector<uint16_t> prop_ids, ScanMode mode,
                        std::vector<PropertyValue> eq_values, std::optional<std::vector<PropertyValue>> range_start,
                        std::optional<std::vector<PropertyValue>> range_end,
                        std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs,
                        std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids = {});

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;

private:
    std::string variable_;
    LabelId label_id_;
    std::vector<uint16_t> prop_ids_;
    ScanMode mode_;
    std::vector<PropertyValue> eq_values_;                  // equality scan values
    std::optional<std::vector<PropertyValue>> range_start_; // range start (composite)
    std::optional<std::vector<PropertyValue>> range_end_;   // range end (composite or single)
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids_;
};

} // namespace compute
} // namespace eugraph
