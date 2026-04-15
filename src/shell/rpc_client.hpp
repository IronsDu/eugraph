#pragma once

#include "gen-cpp2/EuGraphService.h"

#include <string>

namespace eugraph {
namespace shell {

/// RPC client that connects to a remote EuGraph server via fbthrift.
class EuGraphRpcClient {
public:
    EuGraphRpcClient(const std::string& host, int port);

    ~EuGraphRpcClient();

    bool connect();

    // DDL
    thrift::LabelInfo createLabel(const std::string& name,
                                  const std::vector<thrift::PropertyDefThrift>& properties);
    std::vector<thrift::LabelInfo> listLabels();

    thrift::EdgeLabelInfo createEdgeLabel(const std::string& name,
                                           const std::vector<thrift::PropertyDefThrift>& properties);
    std::vector<thrift::EdgeLabelInfo> listEdgeLabels();

    // DML
    thrift::QueryResult executeCypher(const std::string& query);

private:
    std::string host_;
    int port_;
    std::shared_ptr<apache::thrift::Client<thrift::EuGraphService>> client_;
    std::shared_ptr<apache::thrift::ThriftServer> server_; // keeps event base alive
    folly::EventBase* evb_ = nullptr;
};

} // namespace shell
} // namespace eugraph
