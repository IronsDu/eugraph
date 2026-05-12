#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundRemoveOp {
    enum class ItemKind {
        REMOVE_PROPERTY,
        REMOVE_LABEL
    };
    struct RemoveItem {
        ItemKind kind;
        std::string target_variable;
        uint16_t prop_id;
        std::optional<LabelId> label_id;
    };
    std::vector<RemoveItem> items;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
