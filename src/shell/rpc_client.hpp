#pragma once

#include "gen-cpp2/EuGraphService.h"

#include <folly/io/async/EventBase.h>

#include <memory>
#include <string>

namespace eugraph {
namespace shell {

/// RPC client that connects to a remote EuGraph server via fbthrift.
/// Uses coroutine (co_*) client methods with folly::coro::blockingWait.
class EuGraphRpcClient {
public:
    /// Create client that will connect to host:port (for shell use).
    EuGraphRpcClient(const std::string& host, int port);

    /// Create client with an already-connected thrift client (for test use).
    explicit EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client);

    ~EuGraphRpcClient();

    bool connect();

    // DDL
    thrift::LabelInfo createLabel(const std::string& name, const std::vector<thrift::PropertyDefThrift>& properties);
    std::vector<thrift::LabelInfo> listLabels();

    thrift::EdgeLabelInfo createEdgeLabel(const std::string& name,
                                          const std::vector<thrift::PropertyDefThrift>& properties);
    std::vector<thrift::EdgeLabelInfo> listEdgeLabels();

    // DML
    thrift::QueryResult executeCypher(const std::string& query);

private:
    std::string host_;
    int port_;
    std::unique_ptr<folly::EventBase> evb_;
    std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client_;
};

} // namespace shell
} // namespace eugraph
