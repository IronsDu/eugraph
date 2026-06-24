#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Specification of a single output column produced by ProjectionExtractPhysicalOp.
///
/// The operator's output schema is exactly the list of ColumnSpec handed to it.
/// Each spec carries enough information to produce its column independently:
/// either by copying an upstream column (Passthrough), by issuing storage I/O
/// for a single property or label set, by mapping an edge label id to its name,
/// or by constructing a full semantic object (VertexValue/EdgeValue) when the
/// downstream consumer requires the entire entity.
struct ColumnSpec {
    enum class Kind : uint8_t {
        /// Copy upstream column verbatim. Used for variables that downstream
        /// references by their topology/semantic form already present upstream
        /// (e.g. an intermediate variable kept alive across multiple MATCHes).
        Passthrough,
        /// Load a single vertex property via store.getVertexProperty.
        LoadVertexProp,
        /// Load a single edge property via store.getEdgeProperty.
        LoadEdgeProp,
        /// Load vertex labels and materialize as List<String>. Triggered by
        /// labels(n) or by n::Label expressions.
        LoadVertexLabels,
        /// Map edge's label_id to its name via in-memory label dictionary.
        /// No storage I/O. Triggered by type(r).
        LoadEdgeType,
        /// Construct a full VertexValue (labels + all properties). Triggered
        /// by RETURN n, SET/REMOVE convenience mode, BoundDynamicPropertyRef.
        ConstructVertex,
        /// Construct a full EdgeValue (properties). Triggered by RETURN r,
        /// BoundDynamicPropertyRef on edges.
        ConstructEdge,
    };

    Kind kind = Kind::Passthrough;
    std::string output_name;
    binder::BoundType output_type;
    /// Upstream column index from which VertexRef/VertexValue/EdgeKey/EdgeValue
    /// is read. For Passthrough, the column is copied directly.
    size_t source_col = 0;
    /// Label id for LoadVertexProp / ConstructVertex.
    LabelId label_id = INVALID_LABEL_ID;
    /// Edge label id for LoadEdgeProp / LoadEdgeType / ConstructEdge.
    EdgeLabelId edge_label_id = INVALID_EDGE_LABEL_ID;
    /// Property id for LoadVertexProp / LoadEdgeProp.
    uint16_t prop_id = 0;
};

/// Unified schema-reshaping operator that loads vertex/edge properties and
/// labels on demand per downstream requirements.
///
/// The operator takes a child producing topology columns (VertexRef, EdgeKey,
/// or already-upgraded VertexValue/EdgeValue) and outputs exactly the columns
/// described by its specs_. This is the physical realization of the Project
/// semantic described in docs/query/engine/physical-operator-audit.md: the
/// output schema is the downstream requirement, with each column sourced from
/// either passthrough, per-property storage I/O, label/type name lookup, or
/// full-entity construction.
class ProjectionExtractPhysicalOp : public PhysicalOperator {
public:
    ProjectionExtractPhysicalOp(std::vector<ColumnSpec> specs, IAsyncGraphDataStore& store,
                                std::unordered_map<LabelId, std::string> vertex_label_names,
                                std::unordered_map<EdgeLabelId, std::string> edge_label_names, Schema input_schema,
                                std::vector<binder::BoundType> output_types, std::unique_ptr<PhysicalOperator> child)
        : specs_(std::move(specs)), store_(store), vertex_label_names_(std::move(vertex_label_names)),
          edge_label_names_(std::move(edge_label_names)), input_schema_(std::move(input_schema)),
          output_types_(std::move(output_types)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<ColumnSpec> specs_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, std::string> vertex_label_names_;
    std::unordered_map<EdgeLabelId, std::string> edge_label_names_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
