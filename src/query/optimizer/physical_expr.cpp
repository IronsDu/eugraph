#include "query/optimizer/physical_expr.hpp"

#include "query/optimizer/memo.hpp"
#include "query/optimizer/operator_eq.hpp"
#include "query/optimizer/operator_hash.hpp"

namespace eugraph {
namespace optimizer {

uint64_t hashPhysicalExpr(const PhysicalExpr& phys) {
    // Combine tag with source content hash. Mirrors Columbia's
    // OP::hash + M_EXPR::hash pattern.
    uint64_t h = static_cast<uint64_t>(phys.tag);
    h ^= hashBoundLogicalOperator(phys.source);
    h *= 1099511628211ULL; // FNV prime
    return h;
}

bool equalPhysicalExpr(const PhysicalExpr& a, const PhysicalExpr& b) {
    if (a.tag != b.tag)
        return false;
    return equalBoundLogicalOperator(a.source, b.source);
}

PhysicalExpr clonePhysicalExpr(const PhysicalExpr& src) {
    PhysicalExpr result;
    result.tag = src.tag;
    result.source = cloneBoundLogicalOperator(src.source);
    return result;
}

} // namespace optimizer
} // namespace eugraph
