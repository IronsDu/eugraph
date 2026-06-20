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
    h ^= std::hash<std::string>{}(phys.enrich_variable);
    h *= 1099511628211ULL;
    for (const auto& [var, req] : phys.output_mat) {
        h ^= std::hash<std::string>{}(var);
        h ^= req.need_labels;
        for (const auto& [lid, props] : req.need_props) {
            h ^= static_cast<uint64_t>(lid);
            for (uint16_t pid : props)
                h ^= static_cast<uint64_t>(pid);
        }
    }
    h *= 1099511628211ULL;
    for (size_t i = 0; i < phys.required_input_mat.size(); ++i) {
        h ^= static_cast<uint64_t>(i) + 0x9e3779b97f4a7c15ULL;
        for (const auto& [var, req] : phys.required_input_mat[i]) {
            h ^= std::hash<std::string>{}(var);
            h ^= req.need_labels;
            for (const auto& [lid, props] : req.need_props) {
                h ^= static_cast<uint64_t>(lid);
                for (uint16_t pid : props)
                    h ^= static_cast<uint64_t>(pid);
            }
        }
    }
    return h;
}

bool equalPhysicalExpr(const PhysicalExpr& a, const PhysicalExpr& b) {
    if (a.tag != b.tag)
        return false;
    if (a.enrich_variable != b.enrich_variable)
        return false;
    if (a.output_mat != b.output_mat)
        return false;
    if (a.required_input_mat.size() != b.required_input_mat.size())
        return false;
    for (size_t i = 0; i < a.required_input_mat.size(); ++i) {
        if (a.required_input_mat[i] != b.required_input_mat[i])
            return false;
    }
    return equalBoundLogicalOperator(a.source, b.source);
}

PhysicalExpr clonePhysicalExpr(const PhysicalExpr& src) {
    PhysicalExpr result;
    result.tag = src.tag;
    result.source = cloneBoundLogicalOperator(src.source);
    result.enrich_variable = src.enrich_variable;
    result.output_mat = src.output_mat; // map/set copy
    result.required_input_mat = src.required_input_mat;
    return result;
}

} // namespace optimizer
} // namespace eugraph
