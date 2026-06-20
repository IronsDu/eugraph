#pragma once

#include "query/optimizer/cbo.hpp"

#include <vector>

namespace eugraph {
namespace optimizer {

struct LogProp; // forward declaration — full definition in log_prop.hpp

// ============================================================
// PhysicalOpTag — identifies a physical operator type (Columbia: OP_TAG)
//
// Pre-declared in Phase 3 so cost formulas can be defined before
// PhysicalExpr is introduced in Phase 4. Phase 4's implementation
// rules will attach these tags to physical expressions.
// ============================================================
enum class PhysicalOpTag {
    // Scans
    NodeScan,
    LabelScan,
    IndexScan,
    EdgeIndexScan,
    // Leaf sources (Phase 4)
    Singleton,
    CorrelatedSource,
    // Unary
    Filter,
    Project,
    Expand,
    VarLenExpand,
    MultiExpand,
    // Joins
    HashJoin,
    NestedLoopJoin,
    CrossProduct,
    SemiJoin,
    LeftJoin,
    // Streaming
    Sort,
    Limit,
    Skip,
    Distinct,
    Aggregate,
    // Path / list
    PathBuild,
    Unwind,
    // Enrichers (Phase B) — property/label materialisation enforcers.
    // Each upgrades a topology-form variable (VertexRef/EdgeKey/PathTopology)
    // to its semantic counterpart with the required labels and properties
    // loaded into the column buffer.
    VertexEnrich,
    EdgeEnrich,
    PathEnrich,
    // Property extractors (Phase F) — extract only the needed properties as
    // flat columnar vectors (StringVector, Int64Vector, ...) without
    // constructing heavy semantic objects. Used for 90% of queries that only
    // need individual properties rather than full VertexValue/EdgeValue.
    VertexPropertyExtract,
    EdgePropertyExtract,
    PathPropertyExtract,
};

// ============================================================
// findLocalCost — local cost of a physical operator
//
// Computes the cost contribution of this operator (not including
// input costs, which are summed separately by OInputsTask).
//
// Formulas:
//   Scan/IndexScan:    output cardinality
//   Filter/Project:    input cardinality (scan + apply)
//   Expand:            output cardinality (output = input × degree)
//   HashJoin:          build + probe = right.card + left.card
//   NestedLoopJoin:    left × right
//   CrossProduct:      left × right
//   Sort:              n × log2(n), n = input cardinality
//   Limit/Skip:        output cardinality
//   Aggregate:         input cardinality (hash aggregation)
//   Distinct:          input cardinality
// ============================================================
Cost findLocalCost(PhysicalOpTag tag, const LogProp& group_lp, const std::vector<LogProp>& input_lps);

} // namespace optimizer
} // namespace eugraph
