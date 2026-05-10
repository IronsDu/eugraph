#include "program/shell/rpc_client.hpp"

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
    : host_(host), port_(port), evb_(std::make_unique<folly::EventBase>()),
      evb_thread_([this] { evb_->loopForever(); }) {
    evb_->waitUntilRunning();
}

EuGraphRpcClient::EuGraphRpcClient(std::unique_ptr<apache::thrift::Client<thrift::EuGraphService>> client)
    : port_(0), evb_(std::make_unique<folly::EventBase>()), evb_thread_([this] { evb_->loopForever(); }),
      client_(std::move(client)) {
    evb_->waitUntilRunning();
}

EuGraphRpcClient::~EuGraphRpcClient() {
    evb_->runInEventBaseThread([this] { evb_->terminateLoopSoon(); });
    evb_thread_.join();
}

bool EuGraphRpcClient::connect() {
    if (client_) {
        return true;
    }

    try {
        auto t0 = nowMs();
        evb_->runInEventBaseThreadAndWait([&] {
            auto socket = folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(evb_.get(), host_.c_str(), port_));
            auto channel = apache::thrift::RocketClientChannel::newChannel(std::move(socket));
            channel->setTimeout(30000);
            client_ = std::make_unique<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        });
        auto t1 = nowMs();
        spdlog::info("[rpc_client] connect: total={}ms", t1 - t0);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

// ==================== Graph Management ====================

thrift::GraphInfo EuGraphRpcClient::createGraph(const std::string& name) {
    return client_->semifuture_createGraph(name).via(evb_.get()).get();
}

bool EuGraphRpcClient::dropGraph(const std::string& name) {
    return client_->semifuture_dropGraph(name).via(evb_.get()).get();
}

std::vector<thrift::GraphInfo> EuGraphRpcClient::listGraphs() {
    return client_->semifuture_listGraphs().via(evb_.get()).get();
}

// ==================== DDL ====================

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                const std::vector<thrift::PropertyDefThrift>& properties,
                                                const std::string& graph_name) {
    return client_->semifuture_createLabel(name, properties, graph_name).via(evb_.get()).get();
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels(const std::string& graph_name) {
    return client_->semifuture_listLabels(graph_name).via(evb_.get()).get();
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                        const std::vector<thrift::PropertyDefThrift>& properties,
                                                        const std::string& graph_name) {
    return client_->semifuture_createEdgeLabel(name, properties, graph_name).via(evb_.get()).get();
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels(const std::string& graph_name) {
    return client_->semifuture_listEdgeLabels(graph_name).via(evb_.get()).get();
}

// ==================== DML ====================

apache::thrift::ResponseAndClientBufferedStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>
EuGraphRpcClient::executeCypher(const std::string& query, const std::string& graph_name) {
    return client_->semifuture_executeCypher(query, graph_name).via(evb_.get()).get();
}

folly::coro::Task<apache::thrift::ResponseAndClientBufferedStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>>
EuGraphRpcClient::co_executeCypher(const std::string& query, const std::string& graph_name) {
    co_return client_->semifuture_executeCypher(query, graph_name).via(evb_.get()).get();
}

// ==================== Batch Import ====================

thrift::BatchInsertVerticesResult EuGraphRpcClient::batchInsertVertices(const std::string& label_name,
                                                                        std::vector<thrift::VertexRecord> records,
                                                                        const std::string& graph_name) {
    auto t0 = nowMs();
    auto resp =
        client_->semifuture_batchInsertVertices(label_name, std::move(records), graph_name).via(evb_.get()).get();
    spdlog::info("[rpc_client] batchInsertVertices: {} records, {}ms", *resp.count(), nowMs() - t0);
    return resp;
}

std::int32_t EuGraphRpcClient::batchInsertEdges(const std::string& edge_label_name,
                                                std::vector<thrift::EdgeRecord> records,
                                                const std::string& graph_name) {
    auto t0 = nowMs();
    auto resp = client_->semifuture_batchInsertEdges(edge_label_name, std::move(records), graph_name)
                    .via(evb_.get())
                    .get();
    spdlog::info("[rpc_client] batchInsertEdges: {} records, {}ms", resp, nowMs() - t0);
    return resp;
}

} // namespace shell
} // namespace eugraph
