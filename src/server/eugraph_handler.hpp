#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Task.h>

#include <unordered_map>

namespace eugraph {
namespace server {

/// Service handler that implements the Thrift-generated EuGraphService interface.
/// Uses fbthrift coroutine (co_*) methods for async request handling.
/// DDL operations coordinate between meta store and data store.
class EuGraphHandler : public apache::thrift::ServiceHandler<thrift::EuGraphService> {
public:
    EuGraphHandler(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, compute::QueryExecutor& executor)
        : async_data_(async_data), async_meta_(async_meta), executor_(executor) {}

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
    thrift::ResultValue valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                                      const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs);

    static ::eugraph::PropertyType toPropertyType(thrift::PropertyType t);
    static PropertyDef toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id);

    IAsyncGraphDataStore& async_data_;
    IAsyncGraphMetaStore& async_meta_;
    compute::QueryExecutor& executor_;
};

} // namespace server
} // namespace eugraph
