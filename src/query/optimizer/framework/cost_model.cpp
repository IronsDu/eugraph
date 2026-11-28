#include "query/optimizer/framework/cost_model.hpp"

#include "common/config/env_vars.hpp"

#include "query/optimizer/framework/log_prop.hpp"
#include "query/optimizer/framework/physical_expr.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace eugraph {
namespace optimizer {

Cost findLocalCost(PhysicalOpTag tag, const LogProp& group_lp, const std::vector<LogProp>& input_lps,
                   const PhysicalExpr* phys) {
    auto input0Card = [&]() -> double { return input_lps.empty() ? 0.0 : input_lps[0].cardinality; };
    auto input1Card = [&]() -> double { return input_lps.size() < 2 ? 0.0 : input_lps[1].cardinality; };

    switch (tag) {
    // Scans: cost proportional to rows scanned
    case PhysicalOpTag::NodeScan:
    case PhysicalOpTag::LabelScan:
    case PhysicalOpTag::EdgeIndexScan:
        return Cost(group_lp.cardinality);
    case PhysicalOpTag::IndexScan: {
        // Index access has a descriptor-driven selectivity. Until P3 stats are
        // available, use the same conventions as LogProp's index estimates so a
        // range access path costs the same on both sides of the comparison.
        double selectivity = 1.0;
        if (phys != nullptr) {
            if (const auto* scan = std::get_if<binder::BoundLabelScanOp>(&phys->source); scan && scan->index_scan) {
                if (!scan->index_eq_values.empty())
                    selectivity = LogPropDeriver::kIndexedSelectivity;
                else if (scan->index_lower || scan->index_upper)
                    selectivity = LogPropDeriver::kIndexRangeSelectivity;
            }
        }
        return Cost(group_lp.cardinality * selectivity);
    }

    // Leaf sources
    case PhysicalOpTag::Singleton:
        return Cost(1.0);
    case PhysicalOpTag::CorrelatedSource:
        return Cost(1.0); // driven by outer scope

    // Unary operators that scan input and apply
    case PhysicalOpTag::Filter:
    case PhysicalOpTag::Project:
    case PhysicalOpTag::Distinct:
    case PhysicalOpTag::Aggregate:
        return Cost(input0Card());

    // Expand: output is input × degree, cost is output size
    case PhysicalOpTag::Expand:
    case PhysicalOpTag::VarLenExpand:
    case PhysicalOpTag::MultiExpand:
        return Cost(group_lp.cardinality);

    // Nested loop join: left × right iterations.
    // Hash join: build the chosen side once, stream the other. The build side
    // carries a small memory premium so the optimizer prefers the smaller side
    // (the physical operator no longer chooses). kBuildMemoryPremium is a
    // placeholder until memory-aware cost (P3).
    case PhysicalOpTag::HashJoin: {
        constexpr double kBuildMemoryPremium = 1.1;
        const bool build_left = phys != nullptr && phys->build_left;
        const double build = build_left ? input0Card() : input1Card();
        const double probe = build_left ? input1Card() : input0Card();
        // Missing label statistics surface as a zero estimate; an empty side is
        // also never worth a hash table (CrossProduct is cheaper). Treat both as
        // infeasible so the optimizer never picks HashJoin on unknown estimates.
        if (build <= 0.0 || probe <= 0.0)
            return Cost::infinity();
        if (build > static_cast<double>(joinBuildRowBudget()))
            return Cost::infinity();
        return Cost(kBuildMemoryPremium * build + probe);
    }

    // IndexJoin replaces Filter(Expand(chain)) with index seek + reverse chain
    // + join. Local cost is deliberately lower than the forward filter so the
    // candidate is considered; the physical lowering still validates shape and
    // falls back when no usable index/chain exists.
    case PhysicalOpTag::PatternComprehensionApply:
        return Cost(group_lp.cardinality);
    case PhysicalOpTag::IndexJoin: {
        // Only the dynamic-key reverse path remains a candidate (static reverse
        // paths need index distribution statistics to be priced; see
        // physical-planner-residue-audit J8). That path is driven by the source
        // keys -- one IndexScanValues seek per key plus the reverse chain -- so
        // its cost scales with the key count, not with the index size.
        // kDynamicProbeSeekCost approximates a per-key seek against a forward
        // expansion; it must be replaced by key-count/index-cost statistics (P3).
        constexpr double kDynamicProbeSeekCost = 0.001;
        return Cost(input0Card() * kDynamicProbeSeekCost);
    }

    case PhysicalOpTag::IndexProbe:
        // Index range scan + one edge step: proportional to the probe's own
        // estimated output (label rows x range selectivity x degree).
        return Cost(group_lp.cardinality);
    case PhysicalOpTag::IndexProbeJoin:
        // Build the anchor side, probe with the index stream: build + probe.
        return Cost(input0Card() + input1Card());

    case PhysicalOpTag::NestedLoopJoin:
    case PhysicalOpTag::CrossProduct:
        return Cost(input0Card() * input1Card());

    // Semi/left join: dominated by probe
    case PhysicalOpTag::SemiJoin:
    case PhysicalOpTag::LeftJoin:
        return Cost(input0Card() + input1Card());

    // Union: concatenate both inputs
    case PhysicalOpTag::Union:
        return Cost(input0Card() + input1Card());

    // Sort: n log n
    case PhysicalOpTag::Sort: {
        double n = input0Card();
        if (n <= 1.0)
            return Cost(1.0);
        return Cost(n * std::log2(n));
    }

    // Limit/Skip: cheap, proportional to output
    case PhysicalOpTag::Limit:
    case PhysicalOpTag::Skip:
        return Cost(group_lp.cardinality);

    // PathBuild/Unwind: proportional to output
    case PhysicalOpTag::PathBuild:
    case PhysicalOpTag::Unwind:
        return Cost(group_lp.cardinality);

    // Enrichers (Phase B): per-row materialisation cost. Full object construction
    // costs more than flat extraction because of map/heap allocation overhead.
    case PhysicalOpTag::VertexEnrich:
    case PhysicalOpTag::EdgeEnrich:
    case PhysicalOpTag::PathEnrich:
        return Cost(input0Card() * 1.5);

    // Property extractors (Phase F): flat columnar output, no heap allocation.
    // Cheaper than Enrichers — just sequential typed vector writes.
    case PhysicalOpTag::VertexPropertyExtract:
    case PhysicalOpTag::EdgePropertyExtract:
        return Cost(input0Card() * 1.1);
    case PhysicalOpTag::PathPropertyExtract:
        return Cost(input0Card() * 1.2); // extra cost for flatten+dedup
    case PhysicalOpTag::PathCount:
        // One pass over the prefix rows; final adjacency is counted, not
        // materialised. Keep it attractive versus Expand -> Filter -> Aggregate.
        return Cost(input0Card() * 1.05);
    }
    return Cost(0.0);
}

size_t joinBuildRowBudget() {
    const char* env = std::getenv(config::kEnvHashJoinMaxBuildRows);
    if (env == nullptr || *env == '\0')
        return 1'000'000;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(env, &end, 10);
    if (end == env || end == nullptr || *end != '\0')
        return 1'000'000;
    return static_cast<size_t>(parsed);
}

} // namespace optimizer
} // namespace eugraph
