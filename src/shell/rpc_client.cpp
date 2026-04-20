#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include <folly/io/async/AsyncSocket.h>

#include <spdlog/spdlog.h>

#include <chrono>

namespace eugraph {
namespace shell {

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

EuGraphRpcClient::EuGraphRpcClient(const std::string& host, int port)
    : host_(host), port_(port), evb_(std::make_unique<folly::EventBase>()) {}

EuGraphRpcClient::EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client)
    : port_(0), evb_(std::make_unique<folly::EventBase>()), client_(std::move(client)) {}

EuGraphRpcClient::~EuGraphRpcClient() = default;

bool EuGraphRpcClient::connect() {
    if (client_) {
        return true;
    }

    try {
        auto t0 = nowMs();
        auto socket = folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(evb_.get(), host_.c_str(), port_));
        auto t1 = nowMs();
        auto channel = apache::thrift::RocketClientChannel::newChannel(std::move(socket));
        auto t2 = nowMs();
        channel->setTimeout(30000);
        client_ = std::make_unique<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        auto t3 = nowMs();
        spdlog::info("[rpc_client] connect: socket={}ms, channel={}ms, client={}ms, total={}ms",
                     t1 - t0, t2 - t1, t3 - t2, t3 - t0);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                const std::vector<thrift::PropertyDefThrift>& properties) {
    auto t0 = nowMs();
    thrift::LabelInfo resp;
    client_->sync_createLabel(resp, name, properties);
    spdlog::info("[rpc_client] createLabel: {}ms", nowMs() - t0);
    return resp;
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels() {
    auto t0 = nowMs();
    std::vector<thrift::LabelInfo> resp;
    client_->sync_listLabels(resp);
    spdlog::info("[rpc_client] listLabels: {}ms", nowMs() - t0);
    return resp;
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                        const std::vector<thrift::PropertyDefThrift>& properties) {
    auto t0 = nowMs();
    thrift::EdgeLabelInfo resp;
    client_->sync_createEdgeLabel(resp, name, properties);
    spdlog::info("[rpc_client] createEdgeLabel: {}ms", nowMs() - t0);
    return resp;
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels() {
    auto t0 = nowMs();
    std::vector<thrift::EdgeLabelInfo> resp;
    client_->sync_listEdgeLabels(resp);
    spdlog::info("[rpc_client] listEdgeLabels: {}ms", nowMs() - t0);
    return resp;
}

thrift::QueryResult EuGraphRpcClient::executeCypher(const std::string& query) {
    auto t0 = nowMs();
    thrift::QueryResult resp;
    client_->sync_executeCypher(resp, query);
    spdlog::info("[rpc_client] executeCypher: {}ms", nowMs() - t0);
    return resp;
}

} // namespace shell
} // namespace eugraph
