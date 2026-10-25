#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Leaf physical operator for EXISTS sub-plans. Produces a single-row DataChunk
/// with correlated variable values injected from the outer SemiJoinPhysicalOp
/// before each execution.
class CorrelatedSourcePhysicalOp : public PhysicalOperator {
public:
    CorrelatedSourcePhysicalOp(std::vector<std::string> variables, std::vector<binder::BoundType> types)
        : variables_(std::move(variables)), types_(std::move(types)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "CorrelatedSource";
    }

    /// Set the correlated values for the next execution.
    void setValues(const std::vector<Value>& values) {
        rows_.clear();
        rows_.push_back(values);
    }
    void setRows(std::vector<std::vector<Value>> rows) {
        rows_ = std::move(rows);
    }

    /// Expose the number of correlated columns.
    size_t numColumns() const {
        return variables_.size();
    }

private:
    std::vector<std::string> variables_;
    std::vector<binder::BoundType> types_;
    std::vector<std::vector<Value>> rows_;
};

} // namespace compute
} // namespace eugraph
