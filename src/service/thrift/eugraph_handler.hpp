#pragma once

#include "common/types/graph_types.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"
#include "service/graph_service.hpp"

#include <folly/coro/Task.h>

#include <unordered_map>

namespace eugraph {
namespace service {
namespace thrift {

class EuGraphHandler : public apache::thrift::ServiceHandler<thrift_service::EuGraphService> {
public:
    explicit EuGraphHandler(GraphService& graph_service) : graph_service_(graph_service) {}

    // Graph management
    folly::coro::Task<std::unique_ptr<thrift_service::GraphInfo>> co_createGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<bool> co_dropGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::GraphInfo>>> co_listGraphs() override;

    // Thrift service methods (coroutine interface)
    folly::coro::Task<std::unique_ptr<thrift_service::LabelInfo>>
    co_createLabel(std::unique_ptr<std::string> name,
                   std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
                   std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::LabelInfo>>>
    co_listLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<thrift_service::EdgeLabelInfo>>
    co_createEdgeLabel(std::unique_ptr<std::string> name,
                       std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
                       std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::EdgeLabelInfo>>>
    co_listEdgeLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<apache::thrift::ResponseAndServerStream<thrift_service::QueryStreamMeta, thrift_service::ResultRowBatch>>
    co_executeCypher(std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name,
                     std::unique_ptr<std::map<std::string, std::string>> parameters) override;

    folly::coro::Task<std::unique_ptr<thrift_service::BatchInsertVerticesResult>>
    co_batchInsertVertices(std::unique_ptr<std::string> label_name,
                           std::unique_ptr<std::vector<thrift_service::VertexRecord>> records,
                           std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::int32_t> co_batchInsertEdges(std::unique_ptr<std::string> edge_label_name,
                                                        std::unique_ptr<std::vector<thrift_service::EdgeRecord>> records,
                                                        std::unique_ptr<std::string> graph_name) override;

public:
    thrift_service::ResultValue valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                                      const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs);

private:
    static ::eugraph::PropertyType toPropertyType(thrift_service::PropertyType t);
    static thrift_service::PropertyType fromPropertyType(::eugraph::PropertyType t);
    static PropertyDef toPropertyDef(const thrift_service::PropertyDefThrift& req, uint16_t id);
    static PropertyValue thriftToPropertyValue(const thrift_service::PropertyValueThrift& v);

    GraphService& graph_service_;
};

} // namespace thrift
} // namespace service
} // namespace eugraph
