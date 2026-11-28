#include "query/optimizer/post_cbo/post_cbo_finalize.hpp"

#include "query/optimizer/post_cbo/pattern_comprehension_anchor_share.hpp"

namespace eugraph {
namespace optimizer {

bool applyPostCboRewrites(binder::BoundLogicalOperator& root, binder::SlotAllocator& slot_alloc,
                          std::unordered_map<std::string, binder::SlotId>& name_to_slot) {
    bool changed = false;
    changed = rewritePatternComprehensionAnchorShare(root, slot_alloc, name_to_slot) || changed;
    return changed;
}

} // namespace optimizer
} // namespace eugraph
