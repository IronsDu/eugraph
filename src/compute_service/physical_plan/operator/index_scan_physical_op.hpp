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

namespace eugraph {
namespace compute {

class IndexScanPhysicalOp : public PhysicalOperator {
public:
    enum class ScanMode {
        EQUALITY,
        RANGE
    };

    IndexScanPhysicalOp(std::string variable, LabelId label_id, uint16_t prop_id, ScanMode mode,
                        PropertyValue search_value, std::optional<PropertyValue> range_end, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), label_id_(label_id), prop_id_(prop_id), mode_(mode),
          search_value_(std::move(search_value)), range_end_(std::move(range_end)), store_(store),
          label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override;

private:
    std::string variable_;
    LabelId label_id_;
    uint16_t prop_id_;
    ScanMode mode_;
    PropertyValue search_value_;
    std::optional<PropertyValue> range_end_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

} // namespace compute
} // namespace eugraph
