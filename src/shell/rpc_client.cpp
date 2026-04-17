#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include <folly/coro/BlockingWait.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace shell {

EuGraphRpcClient::EuGraphRpcClient(const std::string& host, int port) : host_(host), port_(port) {}

EuGraphRpcClient::EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client)
    : port_(0), client_(std::move(client)) {}

EuGraphRpcClient::~EuGraphRpcClient() = default;

bool EuGraphRpcClient::connect() {
    if (client_) {
        return true;
    }

    try {
        // Use a stack-based EventBase to connect the socket, then let
        // folly::coro::blockingWait manage the EVB for RPC calls.
        folly::EventBase evb;
        auto socket = folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(&evb, host_.c_str(), port_));

        // Drive EVB to complete TCP handshake
        for (int i = 0; i < 100 && socket->connecting(); ++i) {
            evb.loopOnce(30);
        }

        if (!socket->good()) {
            spdlog::error("Failed to connect to server at {}:{}: socket not connected", host_, port_);
            return false;
        }

        auto channel = apache::thrift::RocketClientChannel::newChannel(std::move(socket));
        channel->setTimeout(5000);
        client_ = std::make_unique<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                const std::vector<thrift::PropertyDefThrift>& properties) {
    return folly::coro::blockingWait(client_->co_createLabel(name, properties));
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels() {
    return folly::coro::blockingWait(client_->co_listLabels());
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                        const std::vector<thrift::PropertyDefThrift>& properties) {
    return folly::coro::blockingWait(client_->co_createEdgeLabel(name, properties));
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels() {
    return folly::coro::blockingWait(client_->co_listEdgeLabels());
}

thrift::QueryResult EuGraphRpcClient::executeCypher(const std::string& query) {
    return folly::coro::blockingWait(client_->co_executeCypher(query));
}

} // namespace shell
} // namespace eugraph
