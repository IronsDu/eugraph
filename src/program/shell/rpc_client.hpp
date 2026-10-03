#pragma once

#include "service/thrift/gen-cpp2/EuGraphService.h"

#include <thrift/lib/cpp2/async/ClientBufferedStream.h>

#include <folly/io/async/EventBase.h>

#include <memory>
#include <mutex>
#include <string>

namespace eugraph {
namespace shell {

class EuGraphRpcClient {
public:
    EuGraphRpcClient(const std::string& host, int port);
    explicit EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift_service::EuGraphService>> client);
    ~EuGraphRpcClient();

    bool connect();

    folly::EventBase* evb() {
        return evb_.get();
    }

    // Graph management
    thrift_service::GraphInfo createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<thrift_service::GraphInfo> listGraphs();

    // DDL
    /// pk_props = 主键属性名（有序）；空 = 无主键。
    /// merge_properties 非空 = 对已存在标签增量加属性（同名跳过、类型冲突报错）。
    thrift_service::LabelInfo createLabel(const std::string& name,
                                          const std::vector<thrift_service::PropertyDefThrift>& properties,
                                          const std::string& graph_name, const std::vector<std::string>& pk_props = {},
                                          const std::vector<thrift_service::PropertyDefThrift>& merge_properties = {});
    std::vector<thrift_service::LabelInfo> listLabels(const std::string& graph_name);

    thrift_service::EdgeLabelInfo createEdgeLabel(const std::string& name,
                                                  const std::vector<thrift_service::PropertyDefThrift>& properties,
                                                  const std::string& graph_name);
    std::vector<thrift_service::EdgeLabelInfo> listEdgeLabels(const std::string& graph_name);

    // DML - returns streaming response
    apache::thrift::ResponseAndClientBufferedStream<thrift_service::QueryStreamMeta, thrift_service::ResultRowBatch>
    executeCypher(const std::string& query, const std::string& graph_name,
                  const std::map<std::string, std::string>& params = {});

    folly::coro::Task<apache::thrift::ResponseAndClientBufferedStream<thrift_service::QueryStreamMeta,
                                                                      thrift_service::ResultRowBatch>>
    co_executeCypher(const std::string& query, const std::string& graph_name,
                     const std::map<std::string, std::string>& params = {});

    // Batch import
    thrift_service::BatchInsertVerticesResult batchInsertVertices(const std::string& label_name,
                                                                  std::vector<thrift_service::VertexRecord> records,
                                                                  const std::string& graph_name);
    thrift_service::BatchInsertEdgesResult batchInsertEdges(const std::string& edge_label_name,
                                                            std::vector<thrift_service::EdgeRecord> records,
                                                            const std::string& graph_name);

private:
    std::string host_;
    int port_;
    std::unique_ptr<folly::EventBase> evb_;
    std::thread evb_thread_;
    std::unique_ptr<apache::thrift::Client<thrift_service::EuGraphService>> client_;
    std::mutex rpc_mutex_;
};

} // namespace shell
} // namespace eugraph
