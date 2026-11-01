#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// One executed physical operator in a PROFILE result.
///
/// `inclusive_*` is the operator's own execution interval (including children);
/// `self_*` subtracts the direct children. Storage counters are attributed to the
/// operator that *initiated* the storage call (see StorageOpCounters), so they are
/// not double-counted through parent-child inclusion.
struct ProfileNode {
    std::string operator_type;
    std::string details;
    std::vector<std::string> identifiers;

    uint64_t rows = 0;
    uint64_t chunks = 0;
    uint64_t db_hits = 0;
    uint64_t wt_search = 0;
    uint64_t wt_search_near = 0;
    uint64_t io_dispatches = 0;
    uint64_t prefetch_hits = 0;
    uint64_t prefetch_misses = 0;

    uint64_t inclusive_wall_ns = 0;
    uint64_t inclusive_cpu_ns = 0;
    uint64_t self_wall_ns = 0;
    uint64_t self_cpu_ns = 0;

    std::vector<std::shared_ptr<ProfileNode>> children;
};

/// Statement-level PROFILE summary plus the operator tree.
struct QueryProfile {
    uint64_t planning_ns = 0;
    uint64_t execution_ns = 0;
    uint64_t commit_ns = 0;

    uint64_t rows = 0;
    uint64_t chunks = 0;
    uint64_t db_hits = 0;
    uint64_t wt_search = 0;
    uint64_t wt_search_near = 0;
    uint64_t io_dispatches = 0;
    uint64_t prefetch_hits = 0;
    uint64_t prefetch_misses = 0;

    std::vector<std::shared_ptr<ProfileNode>> roots;
};

} // namespace compute
} // namespace eugraph
