#pragma once

#include "common/types/graph_types.hpp"
#include "service/graph_service.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"

#include <folly/Executor.h>
#include <folly/coro/Task.h>

#include <unordered_map>

namespace eugraph {
namespace service {
namespace thrift {

class EuGraphHandler : public apache::thrift::ServiceHandler<thrift_service::EuGraphService> {
public:
    explicit EuGraphHandler(GraphService& graph_service) : graph_service_(graph_service) {}

    // Graph management
    folly::coro::Task<std::unique_ptr<thrift_service::GraphInfo>>
    co_createGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<bool> co_dropGraph(std::unique_ptr<std::string> name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::GraphInfo>>> co_listGraphs() override;

    // Thrift service methods (coroutine interface)
    folly::coro::Task<std::unique_ptr<thrift_service::LabelInfo>>
    co_createLabel(std::unique_ptr<std::string> name,
                   std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
                   std::unique_ptr<std::string> graph_name, std::unique_ptr<std::vector<std::string>> pk_props,
                   std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> merge_properties) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::LabelInfo>>>
    co_listLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<thrift_service::EdgeLabelInfo>>
    co_createEdgeLabel(std::unique_ptr<std::string> name,
                       std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
                       std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<std::vector<thrift_service::EdgeLabelInfo>>>
    co_listEdgeLabels(std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<
        apache::thrift::ResponseAndServerStream<thrift_service::QueryStreamMeta, thrift_service::ResultRowBatch>>
    co_executeCypher(std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name,
                     std::unique_ptr<std::map<std::string, std::string>> parameters) override;

    /// Query streaming uses the async_tm path instead of the generated
    /// coroutine dispatch: the whole query runs on the shared compute pool and
    /// the callback is completed on the connection EventBase (fbthrift's
    /// WriteBatcher stalls 5-7 s otherwise). Service interceptors are bypassed
    /// for this method; the server does not install any.

    // All handlers are dispatched through async_tm so the work can run on the
    // shared compute pool and the callback can be completed on the connection
    // EventBase. The generated coroutine path would resume on a ThreadManager
    // worker after the first storage await, and fbthrift's WriteBatcher then
    // stalls a request/response reply by 5-7 s (see execution-model.md §3).
    void async_tm_createGraph(apache::thrift::HandlerCallbackPtr<std::unique_ptr<thrift_service::GraphInfo>> callback,
                              std::unique_ptr<std::string> name) override;
    void async_tm_dropGraph(apache::thrift::HandlerCallbackPtr<bool> callback,
                            std::unique_ptr<std::string> name) override;
    void async_tm_listGraphs(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<std::vector<thrift_service::GraphInfo>>> callback) override;
    void
    async_tm_createLabel(apache::thrift::HandlerCallbackPtr<std::unique_ptr<thrift_service::LabelInfo>> callback,
                         std::unique_ptr<std::string> name,
                         std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
                         std::unique_ptr<std::string> graph_name, std::unique_ptr<std::vector<std::string>> pk_props,
                         std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> merge_properties) override;
    void async_tm_listLabels(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<std::vector<thrift_service::LabelInfo>>> callback,
        std::unique_ptr<std::string> graph_name) override;
    void async_tm_createEdgeLabel(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<thrift_service::EdgeLabelInfo>> callback,
        std::unique_ptr<std::string> name, std::unique_ptr<std::vector<thrift_service::PropertyDefThrift>> properties,
        std::unique_ptr<std::string> graph_name) override;
    void async_tm_listEdgeLabels(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<std::vector<thrift_service::EdgeLabelInfo>>> callback,
        std::unique_ptr<std::string> graph_name) override;
    void async_tm_batchInsertVertices(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<thrift_service::BatchInsertVerticesResult>> callback,
        std::unique_ptr<std::string> label_name, std::unique_ptr<std::vector<thrift_service::VertexRecord>> records,
        std::unique_ptr<std::string> graph_name) override;
    void async_tm_batchInsertEdges(
        apache::thrift::HandlerCallbackPtr<std::unique_ptr<thrift_service::BatchInsertEdgesResult>> callback,
        std::unique_ptr<std::string> edge_label_name, std::unique_ptr<std::vector<thrift_service::EdgeRecord>> records,
        std::unique_ptr<std::string> graph_name) override;
    void async_tm_executeCypher(
        apache::thrift::HandlerCallbackPtr<
            apache::thrift::ResponseAndServerStream<thrift_service::QueryStreamMeta, thrift_service::ResultRowBatch>>
            callback,
        std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name,
        std::unique_ptr<std::map<std::string, std::string>> parameters) override;

    folly::coro::Task<std::unique_ptr<thrift_service::BatchInsertVerticesResult>>
    co_batchInsertVertices(std::unique_ptr<std::string> label_name,
                           std::unique_ptr<std::vector<thrift_service::VertexRecord>> records,
                           std::unique_ptr<std::string> graph_name) override;

    folly::coro::Task<std::unique_ptr<thrift_service::BatchInsertEdgesResult>>
    co_batchInsertEdges(std::unique_ptr<std::string> edge_label_name,
                        std::unique_ptr<std::vector<thrift_service::EdgeRecord>> records,
                        std::unique_ptr<std::string> graph_name) override;

public:
    thrift_service::ResultValue valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                                              const std::vector<LabelId>& label_order = {});

private:
    folly::coro::Task<
        apache::thrift::ResponseAndServerStream<thrift_service::QueryStreamMeta, thrift_service::ResultRowBatch>>
    co_executeCypherImpl(std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name,
                         std::unique_ptr<std::map<std::string, std::string>> parameters,
                         folly::Executor::KeepAlive<> compute, bool prefetch_first_chunk);

    static ::eugraph::PropertyType toPropertyType(thrift_service::PropertyType t);
    static thrift_service::PropertyType fromPropertyType(::eugraph::PropertyType t);
    static PropertyDef toPropertyDef(const thrift_service::PropertyDefThrift& req, uint16_t id);
    static PropertyValue thriftToPropertyValue(const thrift_service::PropertyValueThrift& v);

    GraphService& graph_service_;
};

} // namespace thrift
} // namespace service
} // namespace eugraph
