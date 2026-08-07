#include "query/physical_plan/operator/call_physical_op.hpp"

#include "common/types/graph_types.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> CallPhysicalOp::executeChunk() {
    DataChunk output;
    output.setSchema(output_types_);
    output.reserve(1);
    output.count = 1;

    if (procedure_name_ == "db.ping") {
        // CALL db.ping() → single row: {success: true}
        output.columns[0].setValue(0, Value(true));
    } else if (procedure_name_ == "db.schema.visualization") {
        // CALL db.schema.visualization() → single row: {nodes: [], relationships: []}
        ListValue empty_nodes;
        ListValue empty_rels;
        output.columns[0].setValue(0, Value(empty_nodes));
        output.columns[1].setValue(0, Value(empty_rels));
    }
    // Unknown procedures are rejected by the binder, so we should never reach here.

    co_yield output;
}

} // namespace compute
} // namespace eugraph
