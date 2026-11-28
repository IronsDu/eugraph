#include "query/optimizer/rules/logical/resolve_index_probe.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"

namespace eugraph {
namespace optimizer {

PatternNode ResolveIndexProbeRule::pattern() const {
    return {OptNodeType::IndexProbe, {}};
}

std::vector<std::unique_ptr<GroupExpr>> ResolveIndexProbeRule::substitute(GroupExpr& expr, Memo& memo) const {
    const auto* probe_ptr = std::get_if<std::unique_ptr<binder::BoundIndexProbeOp>>(&expr.op);
    if (!probe_ptr || !*probe_ptr)
        return {};
    const binder::BoundIndexProbeOp& probe = **probe_ptr;
    if (probe.index_id != 0 || probe.index_property_name.empty())
        return {};
    // Only a single explicit destination label makes a single-label scan
    // equivalent to the unlabeled probe. Several labels would need a covering
    // label relation that the catalog does not carry yet.
    if (probe.target_label_ids.size() != 1)
        return {};

    const catalog::Catalog* catalog = memo.getCatalog();
    if (catalog == nullptr)
        return {};
    const LabelId target_label = probe.target_label_ids.front();
    auto label_it = catalog->allLabels().find(target_label);
    if (label_it == catalog->allLabels().end())
        return {};

    for (const auto& index : label_it->second.indexes) {
        if (index.index_id == 0 || index.state != IndexState::PUBLIC || index.encoding_version != 2 ||
            index.accessors.size() != 1)
            continue;
        const auto& accessor = index.accessors.front();
        if (accessor.property_name != probe.index_property_name)
            continue;
        // Weak property access is served by weak accessors; a strong accessor
        // indexes only its declared label's column (strong-weak-hybrid.md §7).
        if (accessor.is_strong)
            continue;

        auto resolved = std::make_unique<binder::BoundIndexProbeOp>(probe);
        resolved->index_id = index.index_id;
        resolved->target_label_ids = {target_label};
        for (const auto& property : label_it->second.properties) {
            if (property.name == probe.index_property_name) {
                resolved->index_prop = property.id;
                break;
            }
        }
        std::vector<std::unique_ptr<GroupExpr>> result;
        result.push_back(
            std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(resolved), std::vector<GroupId>{}));
        return result;
    }
    return {};
}

} // namespace optimizer
} // namespace eugraph
