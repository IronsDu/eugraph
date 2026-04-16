#include "shell/rpc_client.hpp"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include <folly/coro/BlockingWait.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace shell {

EuGraphRpcClient::EuGraphRpcClient(const std::string& host, int port)
    : host_(host), port_(port) {}

EuGraphRpcClient::~EuGraphRpcClient() {
    if (evb_) {
        evb_->runImmediatelyOrRunInEventBaseThreadAndWait([this]() {
            client_.reset();
        });
        evb_->terminateLoopSoon();
    }
    if (evb_thread_.joinable()) {
        evb_thread_.join();
    }
}

bool EuGraphRpcClient::connect() {
    try {
        evb_ = std::make_shared<folly::EventBase>();

        // Start EventBase loop in a background thread
        evb_thread_ = std::thread([this]() {
            evb_->loopForever();
        });

        // Wait until the EVB thread is running
        evb_->waitUntilRunning();

        // Create socket and channel on the EVB thread to ensure proper I/O handling
        evb_->runInEventBaseThreadAndWait([&]() {
            auto socket = folly::AsyncSocket::UniquePtr(
                new folly::AsyncSocket(evb_.get(), host_.c_str(), port_));
            auto channel = apache::thrift::RocketClientChannel::newChannel(std::move(socket));
            channel->setTimeout(5000);
            client_ = std::make_shared<apache::thrift::Client<thrift::EuGraphService>>(std::move(channel));
        });
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to server at {}:{}: {}", host_, port_, e.what());
        return false;
    }
}

namespace {

// Execute an RPC by scheduling the semifuture call on the EVB thread and
// waiting for the result via folly::coro::blockingWait with a ManualExecutor.
// This ensures all I/O happens on the EVB thread while the calling thread blocks.
template <typename T, typename F>
T rpcCall(F&& func, folly::EventBase* evb, std::shared_ptr<apache::thrift::Client<thrift::EuGraphService>> client) {
    folly::Try<T> result;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    // Schedule the RPC on the EVB thread
    evb->runInEventBaseThread([&]() {
        auto sf = func(client);
        std::move(std::move(sf))
            .via(evb)
            .then([&](folly::Try<T> t) {
                std::lock_guard<std::mutex> lock(m);
                result = std::move(t);
                done = true;
                cv.notify_one();
            });
    });

    // Wait for completion
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return done; })) {
        throw std::runtime_error("RPC timed out");
    }

    if (result.hasException()) {
        result.exception().throw_exception();
    }
    return std::move(result.value());
}

} // anonymous namespace

thrift::LabelInfo EuGraphRpcClient::createLabel(const std::string& name,
                                                  const std::vector<thrift::PropertyDefThrift>& properties) {
    return rpcCall<thrift::LabelInfo>(
        [&](auto& c) { return c->semifuture_createLabel(name, properties); },
        evb_.get(), client_);
}

std::vector<thrift::LabelInfo> EuGraphRpcClient::listLabels() {
    return rpcCall<std::vector<thrift::LabelInfo>>(
        [&](auto& c) { return c->semifuture_listLabels(); },
        evb_.get(), client_);
}

thrift::EdgeLabelInfo EuGraphRpcClient::createEdgeLabel(const std::string& name,
                                                          const std::vector<thrift::PropertyDefThrift>& properties) {
    return rpcCall<thrift::EdgeLabelInfo>(
        [&](auto& c) { return c->semifuture_createEdgeLabel(name, properties); },
        evb_.get(), client_);
}

std::vector<thrift::EdgeLabelInfo> EuGraphRpcClient::listEdgeLabels() {
    return rpcCall<std::vector<thrift::EdgeLabelInfo>>(
        [&](auto& c) { return c->semifuture_listEdgeLabels(); },
        evb_.get(), client_);
}

thrift::QueryResult EuGraphRpcClient::executeCypher(const std::string& query) {
    return rpcCall<thrift::QueryResult>(
        [&](auto& c) { return c->semifuture_executeCypher(query); },
        evb_.get(), client_);
}

} // namespace shell
} // namespace eugraph
