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
    socket_->setReadCB(this);
}

void BoltConnection::setBookmarkGenerator(std::function<uint64_t()> fn) {
    session_.setBookmarkGenerator(std::move(fn));
}

void BoltConnection::setBoltPort(uint16_t port) {
    session_.setBoltPort(port);
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
    // Bolt v5.1 chunked transfer encoding:
    //   uint16 chunk_size (big-endian), max 16383
    //   chunk_size bytes of message data
    //   ... more data chunks ...
    //   uint16 0x0000 (end-of-message terminator)
    // Accumulate data chunks until 0x0000, then decode as one message.

    while (read_buf_ && read_buf_->length() >= 2) {
        auto data = read_buf_->data();
        auto len = read_buf_->length();

        uint16_t chunk_size = (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);

        if (chunk_size == 0) {
            // End-of-message terminator
            read_buf_->trimStart(2);

            if (message_accumulator_.empty()) {
                // Stray terminator (or keep-alive noop), ignore
                continue;
            }

            // Decode the assembled message
            std::vector<uint8_t> response;
            try {
                response = folly::coro::blockingWait(
                    session_.processMessage(message_accumulator_.data(), message_accumulator_.size()));
            } catch (const packstream::DecodeError& e) {
                spdlog::error("[bolt] decode error: {}", e.what());
                response = session_.makeFailure("ProtocolError", e.what());
            } catch (const std::exception& e) {
                spdlog::error("[bolt] session error: {}", e.what());
                response = session_.makeFailure("DatabaseError", e.what());
            }

            message_accumulator_.clear();

            if (session_.isClosed()) {
                if (!response.empty())
                    sendResponse(std::move(response));
                closeConnection();
                return;
            }

            sendResponse(std::move(response));
            // Continue loop for pipelined messages
        } else {
            // Data chunk
            if (chunk_size > BOLT_MAX_CHUNK_SIZE) {
                spdlog::warn("[bolt] chunk size {} exceeds max {}, closing", chunk_size, BOLT_MAX_CHUNK_SIZE);
                sendResponse(session_.makeFailure("ProtocolError", "Chunk size exceeds maximum"));
                message_accumulator_.clear();
                closeConnection();
                return;
            }

            size_t needed = size_t(2) + chunk_size; // header + data
            if (len < needed) {
                // Incomplete chunk: wait for more data
                return;
            }

            if (message_accumulator_.size() + chunk_size > BOLT_MAX_MESSAGE_SIZE) {
                spdlog::error("[bolt] assembled message exceeds {} byte limit", BOLT_MAX_MESSAGE_SIZE);
                sendResponse(session_.makeFailure("ProtocolError", "Message size exceeds server limit"));
                message_accumulator_.clear();
                closeConnection();
                return;
            }

            const uint8_t* chunk_data = data + 2;
            message_accumulator_.insert(message_accumulator_.end(), chunk_data, chunk_data + chunk_size);
            read_buf_->trimStart(needed);
            // Continue loop to read next chunk header or terminator
        }
    }
}

void BoltConnection::sendResponse(std::vector<uint8_t> data) {
    if (data.empty())
        return;

    // Detect pre-chunked data: validate that the first 2 bytes form a
    // uint16_be chunk size in [1, BOLT_MAX_CHUNK_SIZE], and that a 0x0000
    // terminator follows at the expected position.
    // Previously we only checked data[0]==0x00 which failed for chunks
    // >= 256 bytes (e.g. db.schema.visualization RECORDs).
    bool pre_chunked = false;
    if (data.size() >= 4) {
        uint16_t chunk_size = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        if (chunk_size > 0 && chunk_size <= BOLT_MAX_CHUNK_SIZE) {
            size_t term_pos = size_t(2) + chunk_size;
            if (term_pos + 2 <= data.size() && data[term_pos] == 0x00 && data[term_pos + 1] == 0x00) {
                pre_chunked = true;
            }
        }
    }

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
    message_accumulator_.clear();

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
    conn->setBookmarkGenerator([this]() { return nextBookmark(); });
    conn->setBoltPort(port_);
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
