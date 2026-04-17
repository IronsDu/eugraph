#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include <folly/io/async/AsyncSocket.h>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace shell {

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
        auto socket = folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(evb_.get(), host_.c_str(), port_));
        auto channel = apache::thrift::RocketClientChannel::newChannel(std::move(socket));
        channel->setTimeout(30000);
        client_ = std::make_unique<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                const std::vector<thrift::PropertyDefThrift>& properties) {
    thrift::LabelInfo resp;
    client_->sync_createLabel(resp, name, properties);
    return resp;
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels() {
    std::vector<thrift::LabelInfo> resp;
    client_->sync_listLabels(resp);
    return resp;
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                        const std::vector<thrift::PropertyDefThrift>& properties) {
    thrift::EdgeLabelInfo resp;
    client_->sync_createEdgeLabel(resp, name, properties);
    return resp;
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels() {
    std::vector<thrift::EdgeLabelInfo> resp;
    client_->sync_listEdgeLabels(resp);
    return resp;
}

thrift::QueryResult EuGraphRpcClient::executeCypher(const std::string& query) {
    thrift::QueryResult resp;
    client_->sync_executeCypher(resp, query);
    return resp;
}

} // namespace shell
} // namespace eugraph
