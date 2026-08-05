#include "bolt/bolt_server.hpp"

#include "bolt/packstream/decoder.hpp"

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/io/async/EventBaseManager.h>
#include <spdlog/spdlog.h>

#include <cstring>

namespace eugraph {
namespace bolt {

// ==================== BoltConnection ====================

BoltConnection::BoltConnection(folly::AsyncSocket::UniquePtr socket, server::GraphService& service)
    : socket_(std::move(socket)), service_(service), session_(service_) {}

BoltConnection::~BoltConnection() {
    spdlog::debug("[bolt] connection destroyed");
}

void BoltConnection::start() {
    folly::SocketAddress peerAddr;
    socket_->getPeerAddress(&peerAddr);
    spdlog::info("[bolt] new connection from {}", peerAddr.describe());
    // Start reading the handshake
    socket_->setReadCB(this);
}

void BoltConnection::getReadBuffer(void** buf, size_t* len) {
    // Allocate a new buffer for each read
    constexpr size_t kBufSize = 65536;
    if (!read_buf_) {
        read_buf_ = folly::IOBuf::create(kBufSize);
    }
    *buf = read_buf_->writableData();
    *len = read_buf_->tailroom();
}

void BoltConnection::readDataAvailable(size_t len) noexcept {
    try {
        if (!read_buf_)
            return;
        read_buf_->append(len);

        if (phase_ == Phase::HANDSHAKE) {
            if (read_buf_->length() >= 20) {
                processHandshake();
                // Handshake done, phase is now MESSAGES.
                // Fall through to check for any remaining data (e.g. HELLO
                // message that arrived in the same TCP packet as handshake).
                if (phase_ == Phase::CLOSED || !read_buf_ || read_buf_->length() == 0)
                    return;
                // Otherwise continue to message processing below.
            } else if (read_buf_->length() >= 8) {
                processHandshake();
                if (phase_ == Phase::CLOSED || !read_buf_ || read_buf_->length() == 0)
                    return;
            } else {
                // Need more data; continue reading
                return;
            }
        }

        if (phase_ == Phase::MESSAGES) {
            processMessage();
        }
    } catch (const std::exception& e) {
        spdlog::error("[bolt] connection error: {}", e.what());
        closeConnection();
    }
}

void BoltConnection::readEOF() noexcept {
    spdlog::debug("[bolt] connection EOF");
    closeConnection();
}

void BoltConnection::readErr(const folly::AsyncSocketException& ex) noexcept {
    spdlog::error("[bolt] read error: {}", ex.what());
    closeConnection();
}

void BoltConnection::writeSuccess() noexcept {
    writing_ = false;
    // If there's more data to write, write it
    // For now, done.
}

void BoltConnection::writeErr(size_t /*bytesWritten*/, const folly::AsyncSocketException& ex) noexcept {
    spdlog::error("[bolt] write error: {}", ex.what());
    closeConnection();
}

void BoltConnection::processHandshake() {
    auto data = read_buf_->data();
    auto len = read_buf_->length();

    auto response = session_.negotiateHandshake(data, len);

    if (response.empty()) {
        // Negotiation failed
        spdlog::error("[bolt] handshake negotiation failed");
        closeConnection();
        return;
    }

    phase_ = Phase::MESSAGES;

    // Trim only the handshake bytes (20 bytes standard).
    // Keep any extra bytes that arrived with the handshake packet
    // — they are part of the next message (HELLO).
    size_t consumed = std::min(len, size_t(20));
    read_buf_->trimStart(consumed);
    if (read_buf_->length() == 0) {
        read_buf_.reset();
    }

    // Handshake response is NOT chunked — write raw bytes directly
    auto buf = folly::IOBuf::copyBuffer(response.data(), response.size());
    socket_->writeChain(this, std::move(buf));
}

void BoltConnection::processMessage() {
    // Bolt v5.1 uses chunked transfer encoding:
    //   uint16 chunk_size (big-endian)
    //   chunk_size bytes of message data
    //   uint16 0x0000 (end-of-message terminator)
    // Multiple chunks per message are possible, but typically messages
    // fit in a single chunk.

    while (read_buf_ && read_buf_->length() >= 2) {
        auto data = read_buf_->data();
        auto len = read_buf_->length();

        // Read chunk header
        uint16_t chunk_size = (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);

        if (chunk_size == 0) {
            // End-of-message: consume the 2-byte terminator
            read_buf_->trimStart(2);
            continue;
        }

        // Need chunk_size bytes + 2 bytes for the terminator
        size_t needed = static_cast<size_t>(chunk_size) + 2 + 2; // data + terminator header
        if (len < needed) {
            // Incomplete: wait for more data
            return;
        }

        // Extract the chunk data (after the 2-byte header)
        const uint8_t* chunk_data = data + 2;
        size_t chunk_len = chunk_size;

        // Verify terminator (should be 0x0000 right after chunk data)
        uint16_t term =
            (static_cast<uint16_t>(chunk_data[chunk_len]) << 8) | static_cast<uint16_t>(chunk_data[chunk_len + 1]);

        if (term != 0) {
            // Multi-chunk message: accumulate and continue
            // For now, only handle single-chunk messages
            spdlog::warn("[bolt] multi-chunk message not yet supported, skipping");
            read_buf_->trimStart(needed);
            continue;
        }

        // Decode the message
        try {
            auto response = folly::coro::blockingWait(session_.processMessage(chunk_data, chunk_len));

            // Consume: chunk header(2) + data(chunk_size) + terminator(2)
            read_buf_->trimStart(needed);
            if (read_buf_->length() == 0)
                read_buf_.reset();

            if (session_.isClosed()) {
                if (!response.empty())
                    sendResponse(std::move(response));
                closeConnection();
                return;
            }

            sendResponse(std::move(response));
            // Continue loop to process any pipelined messages
        } catch (const packstream::DecodeError& e) {
            spdlog::error("[bolt] decode error: {}", e.what());
            read_buf_->trimStart(needed);
            if (read_buf_->length() == 0)
                read_buf_.reset();
            // Send FAILURE and continue processing remaining messages
            auto failure = session_.makeFailure("ProtocolError", e.what());
            sendResponse(std::move(failure));
            // Continue loop
        } catch (const std::exception& e) {
            spdlog::error("[bolt] session error: {}", e.what());
            read_buf_->trimStart(needed);
            if (read_buf_->length() == 0)
                read_buf_.reset();
            auto failure = session_.makeFailure("DatabaseError", e.what());
            sendResponse(std::move(failure));
            // Continue loop
        }
    }
}

void BoltConnection::sendResponse(std::vector<uint8_t> data) {
    if (data.empty())
        return;

    // If the data already starts with a chunk header (first byte is 0x00
    // for small chunks, meaning it's pre-chunked), send as-is. Otherwise,
    // wrap in Bolt v5.1 chunked transfer encoding.
    // PackStream struct markers are 0xB0-0xBF; chunk headers for small
    // messages start with 0x00.
    bool pre_chunked = !data.empty() && data[0] == 0x00;

    std::vector<uint8_t> out;
    if (pre_chunked) {
        out = std::move(data);
    } else {
        uint16_t size = static_cast<uint16_t>(data.size());
        out.reserve(2 + data.size() + 2);
        out.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(size & 0xFF));
        out.insert(out.end(), data.begin(), data.end());
        out.push_back(0x00);
        out.push_back(0x00);
    }

    auto buf = folly::IOBuf::copyBuffer(out.data(), out.size());
    socket_->writeChain(this, std::move(buf));
}

void BoltConnection::closeConnection() {
    if (phase_ == Phase::CLOSED)
        return;
    phase_ = Phase::CLOSED;

    // Keep a self-reference so the connection is not destroyed while
    // we're still inside this method (removeConnection may drop the
    // last shared_ptr).
    auto self = shared_from_this();

    if (server_) {
        server_->removeConnection(this);
    }
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    read_buf_.reset();
    write_buf_.reset();
}

// ==================== BoltServer ====================

BoltServer::BoltServer(server::GraphService& service, uint16_t port) : service_(service), port_(port) {}

BoltServer::~BoltServer() {
    stop();
}

void BoltServer::start() {
    if (running_.exchange(true))
        return;

    thread_ = std::thread([this]() {
        evb_ = folly::EventBaseManager::get()->getEventBase();

        server_socket_ = folly::AsyncServerSocket::newSocket(evb_);
        server_socket_->setReusePortEnabled(true);

        folly::SocketAddress addr;
        addr.setFromLocalPort(port_);
        server_socket_->bind(addr);
        server_socket_->listen(128);
        server_socket_->addAcceptCallback(this, evb_);
        server_socket_->startAccepting();

        spdlog::info("[bolt] server listening on port {}", port_);

        evb_->loopForever();

        spdlog::info("[bolt] server stopped");
    });
}

void BoltServer::stop() {
    if (!running_.exchange(false))
        return;

    if (evb_) {
        evb_->terminateLoopSoon();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_socket_.reset();
    // Close all active connections
    auto conns = std::move(active_connections_);
    for (auto& conn : conns) {
        conn->server_ = nullptr;
        conn->closeConnection();
    }
}

void BoltServer::connectionAccepted(folly::NetworkSocket fd, const folly::SocketAddress& clientAddr,
                                    AcceptInfo /*info*/) noexcept {
    spdlog::info("[bolt] accepted connection from {}", clientAddr.describe());

    auto async_socket = folly::AsyncSocket::newSocket(evb_, fd);
    auto conn = std::make_shared<BoltConnection>(std::move(async_socket), service_);
    conn->server_ = this;
    active_connections_.insert(conn);
    conn->start();
}

void BoltServer::removeConnection(BoltConnection* conn) {
    // Linear search: typical deployment has few connections
    for (auto it = active_connections_.begin(); it != active_connections_.end(); ++it) {
        if (it->get() == conn) {
            active_connections_.erase(it);
            return;
        }
    }
}

void BoltServer::acceptError(const std::exception& ex) noexcept {
    spdlog::error("[bolt] accept error: {}", ex.what());
}

} // namespace bolt
} // namespace eugraph
