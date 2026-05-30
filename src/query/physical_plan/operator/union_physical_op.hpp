#pragma once

#include "query/physical_plan/physical_operator_base.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class UnionPhysicalOp : public PhysicalOperator {
public:
    UnionPhysicalOp(bool all, std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right);

    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override;

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

private:
    bool all_;
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
};

} // namespace compute
} // namespace eugraph
