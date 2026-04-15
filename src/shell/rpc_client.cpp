#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/HeaderClientChannel.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace shell {

EuGraphRpcClient::EuGraphRpcClient(const std::string& host, int port)
    : host_(host), port_(port) {}

EuGraphRpcClient::~EuGraphRpcClient() {
    if (evb_) {
        evb_->terminateLoopSoon();
    }
}

bool EuGraphRpcClient::connect() {
    try {
        auto evb = std::make_shared<folly::EventBase>();
        auto socket = folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(evb.get(), host_.c_str(), port_));
        auto channel = apache::thrift::HeaderClientChannel::newChannel(std::move(socket));
        client_ = std::make_shared<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        evb_ = evb.get();

        // Keep the event base alive
        static std::shared_ptr<folly::EventBase> s_evb;
        s_evb = evb;

        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                  const std::vector<thrift::PropertyDefThrift>& properties) {
    thrift::LabelInfo result;
    client_->sync_createLabel(result, name, properties);
    return result;
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels() {
    std::vector<thrift::LabelInfo> result;
    client_->sync_listLabels(result);
    return result;
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                          const std::vector<thrift::PropertyDefThrift>& properties) {
    thrift::EdgeLabelInfo result;
    client_->sync_createEdgeLabel(result, name, properties);
    return result;
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels() {
    std::vector<thrift::EdgeLabelInfo> result;
    client_->sync_listEdgeLabels(result);
    return result;
}

thrift::QueryResult EuGraphRpcClient::executeCypher(const std::string& query) {
    thrift::QueryResult result;
    client_->sync_executeCypher(result, query);
    return result;
}

} // namespace shell
} // namespace eugraph
