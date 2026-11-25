#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <unordered_map>

namespace eugraph {
namespace compute {

/// Presentation-only symbol for a column. It never participates in planning or
/// execution; it exists so EXPLAIN can render logical/user and PE/internal slots
/// through one lookup instead of guessing from column names.
struct ExplainSymbol {
    enum class Kind {
        Variable,
        Property,
        WholeObject,
        VertexLabels,
        EdgeType,
        Coalesced,
        Internal
    };
    Kind kind = Kind::Internal;
    std::string display;  // message.creationDate
    std::string verbose;  // #slot12 / #pe0x80000001
    std::string variable; // message
    std::string property; // creationDate
    LabelId label = INVALID_LABEL_ID;
    uint16_t prop_id = 0;
    bool internal = false;
};

using ExplainSymbolMap = std::unordered_map<binder::SlotId, ExplainSymbol>;

} // namespace compute
} // namespace eugraph
