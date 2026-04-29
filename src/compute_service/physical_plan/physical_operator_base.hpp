#pragma once

#include "compute_service/executor/row.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

// ==================== Physical Operator Base ====================

class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;
    virtual folly::coro::AsyncGenerator<RowBatch> execute() = 0;
    virtual std::string toString() const = 0;
    virtual std::vector<const PhysicalOperator*> children() const {
        return {};
    }
};

} // namespace compute
} // namespace eugraph
