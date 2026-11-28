#include "query/optimizer/rules/logical/generate_index_scan.hpp"

#include "query/optimizer/analysis/index_condition.hpp"
#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace optimizer {

namespace {

struct IndexScanChoice {
    uint32_t index_id = 0;
    std::vector<uint16_t> prop_ids;
    std::vector<PropertyValue> eq;
    std::optional<PropertyValue> lower;
    std::optional<PropertyValue> upper;
    bool lower_inclusive = false;
    bool upper_inclusive = false;
};

std::optional<IndexScanChoice> chooseIndexScan(const LabelDef& label, const std::vector<IndexableCondition>& conds,
                                               const std::unordered_set<std::string>& anon_props) {
    for (const auto& idx : label.indexes) {
        if (idx.index_id == 0 || idx.state != IndexState::PUBLIC || idx.encoding_version != 2 || idx.accessors.empty())
            continue;
        IndexScanChoice out;
        out.index_id = idx.index_id;
        out.prop_ids = idx.prop_ids;
        size_t used = 0;
        bool ok = true;
        for (size_t i = 0; i < idx.accessors.size(); ++i) {
            const auto& acc = idx.accessors[i];
            const IndexableCondition* cond = nullptr;
            for (size_t c = used; c < conds.size(); ++c)
                if (conds[c].property_name == acc.property_name) {
                    cond = &conds[c];
                    break;
                }
            if (!cond) {
                ok = false;
                break;
            }
            if (acc.is_strong) {
                if (cond->is_strong ? cond->label_id != acc.source_label_id
                                    : (cond->declared_label_count != 1 || cond->label_id != acc.source_label_id ||
                                       anon_props.contains(acc.property_name))) {
                    ok = false;
                    break;
                }
            } else if (cond->is_strong) {
                ok = false;
                break;
            }
            if (i + 1 < idx.accessors.size()) {
                if (cond->op != cypher::BinaryOperator::EQ) {
                    ok = false;
                    break;
                }
                out.eq.push_back(cond->value);
                ++used;
            } else {
                IndexRange range;
                bool has_eq = false;
                size_t consumed = used;
                for (size_t c = used; c < conds.size(); ++c) {
                    if (conds[c].property_name != acc.property_name)
                        continue;
                    const auto& cc = conds[c];
                    ++consumed;
                    if (cc.op == cypher::BinaryOperator::EQ) {
                        out.eq.push_back(cc.value);
                        has_eq = true;
                    } else if (cc.op == cypher::BinaryOperator::GT || cc.op == cypher::BinaryOperator::GTE) {
                        mergeIndexLower(range, cc.value, cc.op == cypher::BinaryOperator::GTE);
                    } else if (cc.op == cypher::BinaryOperator::LT || cc.op == cypher::BinaryOperator::LTE) {
                        mergeIndexUpper(range, cc.value, cc.op == cypher::BinaryOperator::LTE);
                    } else {
                        ok = false;
                        break;
                    }
                }
                if (!ok || consumed != conds.size())
                    ok = false;
                if (!has_eq) {
                    if (range.lower) {
                        out.lower = range.lower->value;
                        out.lower_inclusive = range.lower->inclusive;
                    }
                    if (range.upper) {
                        out.upper = range.upper->value;
                        out.upper_inclusive = range.upper->inclusive;
                    }
                }
            }
        }
        if (ok)
            return out;
    }
    return std::nullopt;
}

void applyIndexAccessPaths(binder::BoundLogicalOperator& op, const catalog::Catalog& catalog,
                           const binder::SlotSemanticMap* semantics) {
    auto* filter_ptr = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (filter_ptr && *filter_ptr) {
        auto* scan = std::get_if<binder::BoundLabelScanOp>(&(*filter_ptr)->child);
        if (scan && scan->label_ids.size() == 1 && !scan->index_scan_values && semantics) {
            auto label_it = catalog.allLabels().find(scan->label_ids[0]);
            if (label_it != catalog.allLabels().end()) {
                std::unordered_set<std::string> anon_props;
                for (const auto& [lid, ld] : catalog.allLabels())
                    if (ld.name == "__anon__") {
                        for (const auto& pd : ld.properties)
                            anon_props.insert(pd.name);
                        break;
                    }
                auto resolver = [&](const binder::BoundColumnRef& ref) -> std::optional<ResolvedProperty> {
                    auto it = semantics->find(ref.slot_id);
                    if (it == semantics->end() || it->second.kind != binder::SlotSemanticKind::Property)
                        return std::nullopt;
                    ResolvedProperty out;
                    out.name = it->second.property_name;
                    out.label_id =
                        it->second.property_labels.empty() ? INVALID_LABEL_ID : it->second.property_labels.front();
                    out.prop_id = it->second.property_ids.empty() ? 0 : it->second.property_ids.front();
                    out.is_strong = it->second.strong;
                    out.declared_label_count = static_cast<int>(it->second.property_labels.size());
                    return out;
                };
                std::vector<const binder::BoundExpression*> conjs;
                std::function<void(const binder::BoundExpression&)> collect = [&](const binder::BoundExpression& e) {
                    auto* b = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&e);
                    if (b && *b && (*b)->op == cypher::BinaryOperator::AND) {
                        collect((*b)->left);
                        collect((*b)->right);
                    } else {
                        conjs.push_back(&e);
                    }
                };
                collect((*filter_ptr)->predicate);
                std::vector<IndexableCondition> conds;
                bool all = true;
                for (const auto* e : conjs) {
                    auto* b = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(e);
                    if (!b || !*b) {
                        all = false;
                        break;
                    }
                    auto c = extractNormalizedIndexableCondition(**b, INVALID_LABEL_ID, resolver);
                    if (!c) {
                        all = false;
                        break;
                    }
                    conds.push_back(std::move(*c));
                }
                if (all) {
                    auto choice = chooseIndexScan(label_it->second, conds, anon_props);
                    if (choice) {
                        scan->index_scan = true;
                        scan->index_id = choice->index_id;
                        scan->index_prop_ids = std::move(choice->prop_ids);
                        scan->index_eq_values = std::move(choice->eq);
                        scan->index_lower = std::move(choice->lower);
                        scan->index_upper = std::move(choice->upper);
                        scan->index_lower_inclusive = choice->lower_inclusive;
                        scan->index_upper_inclusive = choice->upper_inclusive;
                        auto child = std::move((*filter_ptr)->child);
                        op = std::move(child);
                        return;
                    }
                }
            }
        }
    }
    optimizer::forEachLogicalChild(
        op, [&](binder::BoundLogicalOperator& child) { applyIndexAccessPaths(child, catalog, semantics); });
}

} // namespace

PatternNode GenerateIndexScanRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}

std::vector<std::unique_ptr<GroupExpr>> GenerateIndexScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!fp || !*fp || expr.child_groups.empty() || memo.getCatalog() == nullptr)
        return {};
    Group& child = memo.getGroup(expr.child_groups[0]);
    if (child.logical_exprs.size() != 1)
        return {};
    const auto* scan = std::get_if<binder::BoundLabelScanOp>(&memo.getExpr(child.logical_exprs[0]).op);
    if (!scan || scan->label_ids.size() != 1 || scan->index_scan || scan->index_scan_values)
        return {};

    binder::BoundFilterOp temp;
    temp.predicate = cloneBoundExpression((*fp)->predicate);
    temp.uniqueness_scope = (*fp)->uniqueness_scope;
    temp.child = *scan;
    binder::BoundLogicalOperator temp_op = std::make_unique<binder::BoundFilterOp>(std::move(temp));
    applyIndexAccessPaths(temp_op, *memo.getCatalog(), memo.getSlotSemantics());
    auto* chosen = std::get_if<binder::BoundLabelScanOp>(&temp_op);
    if (!chosen || !chosen->index_scan)
        return {};

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(*chosen), std::vector<GroupId>{}));
    return result;
}

} // namespace optimizer
} // namespace eugraph
