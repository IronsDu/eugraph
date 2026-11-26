#include "query/optimizer/analysis/index_condition.hpp"

#include "query/optimizer/framework/memo.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace eugraph {
namespace optimizer {

int compareIndexValues(const PropertyValue& a, const PropertyValue& b) {
    if (auto* ai = std::get_if<int64_t>(&a)) {
        if (auto* bi = std::get_if<int64_t>(&b))
            return *ai < *bi ? -1 : (*ai > *bi ? 1 : 0);
    }
    if (auto* ad = std::get_if<double>(&a)) {
        if (auto* bd = std::get_if<double>(&b))
            return *ad < *bd ? -1 : (*ad > *bd ? 1 : 0);
    }
    if (auto* as = std::get_if<std::string>(&a)) {
        if (auto* bs = std::get_if<std::string>(&b))
            return *as < *bs ? -1 : (*as > *bs ? 1 : 0);
    }
    if (auto* ab = std::get_if<bool>(&a)) {
        if (auto* bb = std::get_if<bool>(&b))
            return *ab < *bb ? -1 : (*ab > *bb ? 1 : 0);
    }
    return 0;
}

void mergeIndexLower(IndexRange& range, const PropertyValue& value, bool inclusive) {
    if (!range.lower || compareIndexValues(value, range.lower->value) > 0) {
        range.lower = IndexBound{value, inclusive};
        return;
    }
    if (compareIndexValues(value, range.lower->value) == 0)
        range.lower->inclusive = range.lower->inclusive && inclusive;
}

void mergeIndexUpper(IndexRange& range, const PropertyValue& value, bool inclusive) {
    if (!range.upper || compareIndexValues(value, range.upper->value) < 0) {
        range.upper = IndexBound{value, inclusive};
        return;
    }
    if (compareIndexValues(value, range.upper->value) == 0)
        range.upper->inclusive = range.upper->inclusive && inclusive;
}

namespace {
std::optional<PropertyValue> valueToPropertyValue(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::optional<PropertyValue> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return std::nullopt;
            else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, double> ||
                               std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<uint8_t>>)
                return PropertyValue{v};
            else
                return std::nullopt;
        },
        value);
}

std::optional<cypher::BinaryOperator> reverseComparison(cypher::BinaryOperator op) {
    switch (op) {
    case cypher::BinaryOperator::LT:
        return cypher::BinaryOperator::GT;
    case cypher::BinaryOperator::LTE:
        return cypher::BinaryOperator::GTE;
    case cypher::BinaryOperator::GT:
        return cypher::BinaryOperator::LT;
    case cypher::BinaryOperator::GTE:
        return cypher::BinaryOperator::LTE;
    case cypher::BinaryOperator::EQ:
        return cypher::BinaryOperator::EQ;
    default:
        return std::nullopt;
    }
}
} // namespace

std::optional<IndexableCondition> extractIndexableCondition(const binder::BoundBinaryOp& binop, LabelId anon_label_id,
                                                            const ColumnPropertyResolver& resolve_column) {
    if (!std::holds_alternative<binder::BoundLiteral>(binop.right))
        return std::nullopt;
    const auto& lit = std::get<binder::BoundLiteral>(binop.right);
    auto pv = valueToPropertyValue(lit.value);
    if (!pv)
        return std::nullopt;

    IndexableCondition cond;
    cond.op = binop.op;
    cond.value = std::move(*pv);

    if (auto* prop = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&binop.left); prop && *prop) {
        if ((*prop)->candidates.empty())
            return std::nullopt;
        LabelId first_label = INVALID_LABEL_ID;
        int declared = 0;
        for (const auto& cand : (*prop)->candidates) {
            if (cand.label_id == anon_label_id || cand.label_id == INVALID_LABEL_ID)
                continue;
            if (declared == 0) {
                first_label = cand.label_id;
                cond.prop_id = cand.prop_id;
            }
            ++declared;
        }
        cond.declared_label_count = declared;
        cond.label_id = declared == 1 ? first_label : (*prop)->candidates.front().label_id;
        cond.prop_id = declared == 1 ? cond.prop_id : (*prop)->candidates.front().prop_id;
        cond.property_name = (*prop)->property_name;
        cond.is_strong = (*prop)->strong_mode;
        return cond;
    }

    if (auto* col = std::get_if<binder::BoundColumnRef>(&binop.left); col) {
        auto resolved = resolve_column(*col);
        if (!resolved)
            return std::nullopt;
        cond.property_name = resolved->name;
        cond.label_id = resolved->label_id;
        cond.prop_id = resolved->prop_id;
        cond.is_strong = resolved->is_strong;
        cond.declared_label_count = resolved->declared_label_count;
        return cond;
    }
    return std::nullopt;
}

std::optional<IndexableCondition> extractNormalizedIndexableCondition(const binder::BoundBinaryOp& binop,
                                                                      LabelId anon_label_id,
                                                                      const ColumnPropertyResolver& resolve_column) {
    if (auto cond = extractIndexableCondition(binop, anon_label_id, resolve_column))
        return cond;
    if (!std::holds_alternative<binder::BoundLiteral>(binop.left))
        return std::nullopt;
    auto reversed = reverseComparison(binop.op);
    if (!reversed)
        return std::nullopt;
    binder::BoundBinaryOp swapped;
    swapped.op = *reversed;
    swapped.left = cloneBoundExpression(binop.right);
    swapped.right = cloneBoundExpression(binop.left);
    return extractIndexableCondition(swapped, anon_label_id, resolve_column);
}

} // namespace optimizer
} // namespace eugraph
