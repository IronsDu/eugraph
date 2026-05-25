#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundSetOp {
    enum class ItemKind {
        SET_PROPERTY,
        SET_PROPERTIES,
        SET_LABELS
    };
    struct SetItem {
        ItemKind kind;
        std::string target_variable;
        std::string prop_name;
        std::optional<uint16_t> prop_id;
        std::optional<BoundExpression> value_expr;
        std::optional<LabelId> label_id;
        bool strong_mode = false;
    };
    std::vector<SetItem> items;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
