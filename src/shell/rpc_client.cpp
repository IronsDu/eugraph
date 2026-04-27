#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include <folly/io/async/AsyncSocket.h>

#include <spdlog/spdlog.h>

#include <chrono>

namespace eugraph {
namespace shell {

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
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
        spdlog::info("[rpc_client] connect: socket={}ms, channel={}ms, client={}ms, total={}ms", t1 - t0, t2 - t1,
                     t3 - t2, t3 - t0);
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

thrift::BatchInsertVerticesResult
EuGraphRpcClient::batchInsertVertices(const std::string& label_name, std::vector<thrift::VertexRecord> records) {
    auto t0 = nowMs();
    thrift::BatchInsertVerticesResult resp;
    client_->sync_batchInsertVertices(resp, label_name, std::move(records));
    spdlog::info("[rpc_client] batchInsertVertices: {} records, {}ms", *resp.count(), nowMs() - t0);
    return resp;
}

std::int32_t EuGraphRpcClient::batchInsertEdges(const std::string& edge_label_name,
                                                 std::vector<thrift::EdgeRecord> records) {
    auto t0 = nowMs();
    auto resp = client_->sync_batchInsertEdges(edge_label_name, std::move(records));
    spdlog::info("[rpc_client] batchInsertEdges: {} records, {}ms", resp, nowMs() - t0);
    return resp;
}

} // namespace shell
} // namespace eugraph
