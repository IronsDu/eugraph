#include "query/physical_plan/operator/late_materialize_physical_op.hpp"

namespace eugraph {
namespace compute {

std::string LateMaterializePhysicalOp::toString() const {
    return "LateMaterialize(deferred output columns after LIMIT)";
}

} // namespace compute
} // namespace eugraph
