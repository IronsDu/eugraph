#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "metadata_service/metadata_service.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/Task.h>

#include <unordered_map>

namespace eugraph {
namespace server {

/// Service handler that implements the Thrift-generated EuGraphService interface.
/// Uses fbthrift coroutine (co_*) methods for async request handling.
class EuGraphHandler : public apache::thrift::ServiceHandler<thrift::EuGraphService> {
public:
    EuGraphHandler(IGraphStore& store, IMetadataService& meta, compute::QueryExecutor& executor)
        : store_(store), meta_(meta), executor_(executor) {}

    // Thrift service methods (coroutine interface)
    folly::coro::Task<std::unique_ptr<thrift::LabelInfo>>
    co_createLabel(std::unique_ptr<std::string> name,
                   std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift::LabelInfo>>> co_listLabels() override;

    folly::coro::Task<std::unique_ptr<thrift::EdgeLabelInfo>>
    co_createEdgeLabel(std::unique_ptr<std::string> name,
                       std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>> co_listEdgeLabels() override;

    folly::coro::Task<std::unique_ptr<thrift::QueryResult>>
    co_executeCypher(std::unique_ptr<std::string> query) override;

private:
    // Convert runtime Value to Thrift ResultValue union.
    // label_defs is used to map prop_id → property name when serializing VertexValue/EdgeValue.
    thrift::ResultValue valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                                      const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs);

    // Convert Thrift PropertyType enum to internal PropertyType
    static ::eugraph::PropertyType toPropertyType(thrift::PropertyType t);

    // Convert Thrift PropertyDefThrift to internal PropertyDef
    static PropertyDef toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id);

    IGraphStore& store_;
    IMetadataService& meta_;
    compute::QueryExecutor& executor_;
};

} // namespace server
} // namespace eugraph
