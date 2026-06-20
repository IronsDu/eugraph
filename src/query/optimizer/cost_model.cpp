#include "query/optimizer/cost_model.hpp"

#include "query/optimizer/log_prop.hpp"

#include <cmath>

namespace eugraph {
namespace optimizer {

Cost findLocalCost(PhysicalOpTag tag, const LogProp& group_lp, const std::vector<LogProp>& input_lps) {
    auto input0Card = [&]() -> double { return input_lps.empty() ? 0.0 : input_lps[0].cardinality; };
    auto input1Card = [&]() -> double { return input_lps.size() < 2 ? 0.0 : input_lps[1].cardinality; };

    switch (tag) {
    // Scans: cost proportional to rows scanned
    case PhysicalOpTag::NodeScan:
    case PhysicalOpTag::LabelScan:
    case PhysicalOpTag::IndexScan:
    case PhysicalOpTag::EdgeIndexScan:
        return Cost(group_lp.cardinality);

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

    // Hash join: build side (right) + probe side (left)
    case PhysicalOpTag::HashJoin:
        return Cost(input1Card() + input0Card());

    // Nested loop join: left × right iterations
    case PhysicalOpTag::NestedLoopJoin:
    case PhysicalOpTag::CrossProduct:
        return Cost(input0Card() * input1Card());

    // Semi/left join: dominated by probe
    case PhysicalOpTag::SemiJoin:
    case PhysicalOpTag::LeftJoin:
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
    }
    return Cost(0.0);
}

} // namespace optimizer
} // namespace eugraph
