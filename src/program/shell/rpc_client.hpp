#pragma once

#include "gen-cpp2/EuGraphService.h"

#include <thrift/lib/cpp2/async/ClientBufferedStream.h>

#include <folly/io/async/EventBase.h>

#include <memory>
#include <string>

namespace eugraph {
namespace shell {

class EuGraphRpcClient {
public:
    EuGraphRpcClient(const std::string& host, int port);
    explicit EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client);
    ~EuGraphRpcClient();

    bool connect();

    folly::EventBase* evb() { return evb_.get(); }

    // Graph management
    thrift::GraphInfo createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<thrift::GraphInfo> listGraphs();

    // DDL
    thrift::LabelInfo createLabel(const std::string& name, const std::vector<thrift::PropertyDefThrift>& properties,
                                  const std::string& graph_name);
    std::vector<thrift::LabelInfo> listLabels(const std::string& graph_name);

    thrift::EdgeLabelInfo createEdgeLabel(const std::string& name,
                                          const std::vector<thrift::PropertyDefThrift>& properties,
                                          const std::string& graph_name);
    std::vector<thrift::EdgeLabelInfo> listEdgeLabels(const std::string& graph_name);

    // DML - returns streaming response
    apache::thrift::ResponseAndClientBufferedStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>
    executeCypher(const std::string& query, const std::string& graph_name);

    folly::coro::Task<apache::thrift::ResponseAndClientBufferedStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>>
    co_executeCypher(const std::string& query, const std::string& graph_name);

    // Batch import
    thrift::BatchInsertVerticesResult batchInsertVertices(const std::string& label_name,
                                                          std::vector<thrift::VertexRecord> records,
                                                          const std::string& graph_name);
    std::int32_t batchInsertEdges(const std::string& edge_label_name, std::vector<thrift::EdgeRecord> records,
                                  const std::string& graph_name);

private:
    std::string host_;
    int port_;
    std::unique_ptr<folly::EventBase> evb_;
    std::thread evb_thread_;
    std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client_;
};

} // namespace shell
} // namespace eugraph
