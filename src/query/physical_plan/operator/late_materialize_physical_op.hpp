#pragma once

#include "query/physical_plan/operator/projection_extract_physical_op.hpp"

namespace eugraph {
namespace compute {

/// Materializes output-only columns after LIMIT.
///
/// Semantically this operator is the second half of a split RETURN Project:
/// the physical planner plans the Project's child (Sort/Filter/Expand) with
/// the output-only PE slots deferred, runs LIMIT, then places this operator
/// above LIMIT to load only the surviving rows' properties (content/imageFile,
/// names, whole objects, ...).  Input columns — including the VertexRef /
/// EdgeKey columns needed to resolve those properties — are passed through
/// unchanged, so no topology column is ever dropped or re-created.
///
/// The operator owns its complete output schema and slot layout (append-only:
/// input columns followed by the deferred columns).  It reuses the proven
/// per-column materialization core of ProjectionExtractPhysicalOp; the
/// distinction is semantic/positional (post-LIMIT, no Filter/Sort below may
/// depend on these columns), not a different loading mechanism.
class LateMaterializePhysicalOp : public ProjectionExtractPhysicalOp {
public:
    using ProjectionExtractPhysicalOp::ProjectionExtractPhysicalOp;

    std::string toString() const override;
};

} // namespace compute
} // namespace eugraph
