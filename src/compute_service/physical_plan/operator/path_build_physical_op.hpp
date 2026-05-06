#pragma once

#include "compute_service/executor/row.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Assembles a PathValue from named row columns and appends it as a new column.
class PathBuildPhysicalOp : public PhysicalOperator {
public:
    PathBuildPhysicalOp(std::string path_var, std::vector<std::string> element_vars, Schema input_schema,
                        std::unique_ptr<PhysicalOperator> child)
        : path_var_(std::move(path_var)), element_vars_(std::move(element_vars)),
          input_schema_(std::move(input_schema)), child_(std::move(child)) {
        // Resolve column indices for each element variable
        for (const auto& ev : element_vars_) {
            int idx = -1;
            for (size_t i = 0; i < input_schema_.size(); ++i) {
                if (input_schema_[i] == ev) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            element_cols_.push_back(idx);
        }
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string path_var_;
    std::vector<std::string> element_vars_;
    std::vector<int> element_cols_; // resolved column indices for each element
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
