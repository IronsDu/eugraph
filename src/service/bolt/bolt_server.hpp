#pragma once

#include "service/bolt/bolt_session.hpp"
#include "service/graph_service.hpp"

#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBaseManager.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace eugraph {
namespace service {
namespace bolt {

class BoltServer;
struct ListenerState;

/// Manages per-connection Bolt protocol state and I/O.
/// Each connection gets one BoltConnection.
class BoltConnection : public folly::AsyncReader::ReadCallback,
                       public folly::AsyncWriter::WriteCallback,
                       public std::enable_shared_from_this<BoltConnection> {
    friend class BoltServer;

public:
    BoltConnection(folly::AsyncSocket::UniquePtr socket, service::GraphService& service);
    ~BoltConnection() override;

    void start();
    void setBookmarkGenerator(std::function<uint64_t()> fn);
    void setBoltPort(uint16_t port);

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
    void dispatchMessage(std::vector<uint8_t> message);
    void finishMessage(std::vector<uint8_t> response);
    void sendResponse(std::vector<uint8_t> data);
    void closeConnection();

    enum class Phase {
        HANDSHAKE,
        MESSAGES,
        CLOSED
    };

    /// Transport-layer protocol detected on this connection.
    enum class Transport {
        DETECTING,    // Peeking first bytes to determine protocol
        BOLT_RAW,     // Traditional raw TCP Bolt (handshake magic + chunked transfer)
        WS_HANDSHAKE, // Reading WebSocket HTTP upgrade request
        WS_FRAMED,    // WebSocket connected; reading/writing frames
    };

    // Transport detection / handshake (called from readDataAvailable)
    void detectProtocol();
    void processWsHandshake();
    void processWsFrame();

    folly::AsyncSocket::UniquePtr socket_;
    service::GraphService& service_;
    BoltSession session_;
    BoltServer* server_ = nullptr; // for removing self from active set

    Transport transport_ = Transport::DETECTING;
    Phase phase_ = Phase::HANDSHAKE;
    std::unique_ptr<folly::IOBuf> read_buf_;
    std::unique_ptr<folly::IOBuf> write_buf_;
    bool writing_ = false;
    std::vector<uint8_t> message_accumulator_;

    // Messages are decoded on the socket EventBase. Query processing is
    // suspended via coroutines, so keep a FIFO of decoded messages and run
    // one BoltSession coroutine at a time per connection.
    std::deque<std::vector<uint8_t>> pending_messages_;
    bool message_processing_ = false;
};

/// Bolt protocol TCP server.
/// Listens on the given port and accepts Neo4j driver connections.
class BoltServer {
public:
    BoltServer(service::GraphService& service, uint16_t port, size_t io_threads = 1);
    ~BoltServer();

    BoltServer(const BoltServer&) = delete;
    BoltServer& operator=(const BoltServer&) = delete;

    /// Start the Bolt server. Non-blocking: starts a background EventBase thread.
    void start();

    /// Stop the Bolt server and wait for the thread to exit.
    void stop();

    uint16_t port() const {
        return port_;
    }

    void removeConnection(BoltConnection* conn);

    uint64_t nextBookmark() {
        return bookmark_counter_.fetch_add(1) + 1;
    }

private:
    friend class ListenerAcceptCallback;

    void handleAccepted(folly::NetworkSocket fd, const folly::SocketAddress& clientAddr,
                        folly::EventBase* evb) noexcept;

    service::GraphService& service_;
    uint16_t port_;
    size_t io_threads_ = 1;

    std::vector<std::unique_ptr<ListenerState>> listeners_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> bookmark_counter_{0};

    std::mutex connections_mu_;
    std::unordered_set<std::shared_ptr<BoltConnection>> active_connections_;
};

class ListenerAcceptCallback : public folly::AsyncServerSocket::AcceptCallback {
public:
    ListenerAcceptCallback(BoltServer* server, folly::EventBase* evb) : server_(server), evb_(evb) {}

    void connectionAccepted(folly::NetworkSocket fd, const folly::SocketAddress& clientAddr,
                            AcceptInfo info) noexcept override {
        server_->handleAccepted(fd, clientAddr, evb_);
    }
    void acceptError(const std::exception& ex) noexcept override;

private:
    BoltServer* server_;
    folly::EventBase* evb_;
};

} // namespace bolt
} // namespace service
} // namespace eugraph
