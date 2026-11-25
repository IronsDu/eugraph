#pragma once

#include "common/schema/graph_schema.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/explain/explain.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/index/index_runtime_registry.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class IndexScanValuesPhysicalOp : public PhysicalOperator {
public:
    IndexScanValuesPhysicalOp(std::string variable, uint32_t index_id, IAsyncGraphDataStore& store,
                              std::vector<binder::BoundType> output_types, Schema output_schema)
        : variable_(std::move(variable)), index_id_(index_id), store_(store), output_types_(std::move(output_types)),
          output_schema_(std::move(output_schema)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "IndexScanValues(variable=" + variable_ + ", index_id=" + std::to_string(index_id_) + ")";
    }
    void explain(ExplainNode& node, const ExplainContext& ctx) const override {
        node.operator_name = "IndexScanValues";
        node.details.push_back("variable=" + variable_);
        if (ctx.schema != nullptr) {
            for (const auto& [lid, def] : ctx.schema->labels) {
                for (const auto& idx : def.indexes) {
                    if (idx.index_id != index_id_)
                        continue;
                    node.details.push_back("label=" + explainLabelName(lid, ctx));
                    if (!idx.name.empty())
                        node.details.push_back("index=" + idx.name);
                    if (!idx.prop_ids.empty())
                        node.details.push_back("properties=[" + explainPropertyList(lid, idx.prop_ids, ctx) + "]");
                    return;
                }
            }
        }
        node.details.push_back("index_id=" + std::to_string(index_id_));
    }
    void setValues(std::shared_ptr<std::vector<PropertyValue>> values) {
        values_ = std::move(values);
    }

private:
    std::string variable_;
    uint32_t index_id_;
    IAsyncGraphDataStore& store_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
    std::shared_ptr<std::vector<PropertyValue>> values_;
    std::optional<IndexReadHandle> read_handle_;
};

} // namespace compute
} // namespace eugraph
