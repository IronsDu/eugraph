#pragma once

#include "common/types/graph_types.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "storage/graph_manager.hpp"

#include <folly/coro/Task.h>

#include <unordered_map>

namespace eugraph {
namespace server {

class EuGraphHandler : public apache::thrift::ServiceHandler<thrift::EuGraphService> {
public:
    explicit EuGraphHandler(GraphManager& graph_manager) : graph_manager_(graph_manager) {}

    // Graph management
    folly::coro::Task<std::unique_ptr<thrift::GraphInfo>> co_createGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<bool> co_dropGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift::GraphInfo>>> co_listGraphs() override;

    // Thrift service methods (coroutine interface)
    folly::coro::Task<std::unique_ptr<thrift::LabelInfo>>
    co_createLabel(std::unique_ptr<std::string> name,
                   std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties,
                   std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift::LabelInfo>>>
    co_listLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<thrift::EdgeLabelInfo>>
    co_createEdgeLabel(std::unique_ptr<std::string> name,
                       std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties,
                       std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>>
    co_listEdgeLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<apache::thrift::ResponseAndServerStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>>
    co_executeCypher(std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<thrift::BatchInsertVerticesResult>>
    co_batchInsertVertices(std::unique_ptr<std::string> label_name,
                           std::unique_ptr<std::vector<thrift::VertexRecord>> records,
                           std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::int32_t> co_batchInsertEdges(std::unique_ptr<std::string> edge_label_name,
                                                        std::unique_ptr<std::vector<thrift::EdgeRecord>> records,
                                                        std::unique_ptr<std::string> graph_name) override;

public:
    thrift::ResultValue valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                                      const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs);

private:
    static ::eugraph::PropertyType toPropertyType(thrift::PropertyType t);
    static thrift::PropertyType fromPropertyType(::eugraph::PropertyType t);
    static PropertyDef toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id);
    static PropertyValue thriftToPropertyValue(const thrift::PropertyValueThrift& v);

    GraphInstance* resolveGraph(const std::string& graph_name);

    GraphManager& graph_manager_;
};

} // namespace server
} // namespace eugraph
