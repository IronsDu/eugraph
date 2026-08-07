#pragma once

#include "bolt/bolt_session.hpp"
#include "server/graph_service.hpp"

#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBaseManager.h>

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>

namespace eugraph {
namespace bolt {

class BoltServer;

/// Manages per-connection Bolt protocol state and I/O.
/// Each connection gets one BoltConnection.
class BoltConnection : public folly::AsyncReader::ReadCallback,
                       public folly::AsyncWriter::WriteCallback,
                       public std::enable_shared_from_this<BoltConnection> {
    friend class BoltServer;

public:
    BoltConnection(folly::AsyncSocket::UniquePtr socket, server::GraphService& service);
    ~BoltConnection() override;

    void start();
    void setBookmarkGenerator(std::function<uint64_t()> fn);

    // ReadCallback
    void getReadBuffer(void** buf, size_t* len) override;
    void readDataAvailable(size_t len) noexcept override;
    void readEOF() noexcept override;
    void readErr(const folly::AsyncSocketException& ex) noexcept override;

    // WriteCallback
    void writeSuccess() noexcept override;
    void writeErr(size_t bytesWritten, const folly::AsyncSocketException& ex) noexcept override;

private:
    void processHandshake();
    void processMessage();
    void sendResponse(std::vector<uint8_t> data);
    void closeConnection();

    enum class Phase {
        HANDSHAKE,
        MESSAGES,
        CLOSED
    };

    folly::AsyncSocket::UniquePtr socket_;
    server::GraphService& service_;
    BoltSession session_;
    BoltServer* server_ = nullptr; // for removing self from active set

    Phase phase_ = Phase::HANDSHAKE;
    std::unique_ptr<folly::IOBuf> read_buf_;
    std::unique_ptr<folly::IOBuf> write_buf_;
    bool writing_ = false;
    std::vector<uint8_t> message_accumulator_;
};

/// Bolt protocol TCP server.
/// Listens on the given port and accepts Neo4j driver connections.
class BoltServer : public folly::AsyncServerSocket::AcceptCallback {
public:
    BoltServer(server::GraphService& service, uint16_t port);
    ~BoltServer() override;

    BoltServer(const BoltServer&) = delete;
    BoltServer& operator=(const BoltServer&) = delete;

    /// Start the Bolt server. Non-blocking: starts a background EventBase thread.
    void start();

    /// Stop the Bolt server and wait for the thread to exit.
    void stop();

    uint16_t port() const {
        return port_;
    }

    // AcceptCallback
    void connectionAccepted(folly::NetworkSocket fd, const folly::SocketAddress& clientAddr,
                            AcceptInfo /*info*/) noexcept override;
    void acceptError(const std::exception& ex) noexcept override;

    void removeConnection(BoltConnection* conn);

    uint64_t nextBookmark() {
        return bookmark_counter_.fetch_add(1) + 1;
    }

private:
    server::GraphService& service_;
    uint16_t port_;

    folly::EventBase* evb_ = nullptr;
    std::shared_ptr<folly::AsyncServerSocket> server_socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> bookmark_counter_{0};
    std::unordered_set<std::shared_ptr<BoltConnection>> active_connections_;
};

} // namespace bolt
} // namespace eugraph
