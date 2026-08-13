#include "service/bolt/bolt_server.hpp"

#include "service/bolt/packstream/decoder.hpp"
#include "service/bolt/websocket.hpp"

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/io/async/EventBaseManager.h>
#include <spdlog/spdlog.h>

#include <cstring>

namespace eugraph {
namespace service {
namespace bolt {

// ==================== BoltConnection ====================

BoltConnection::BoltConnection(folly::AsyncSocket::UniquePtr socket, service::GraphService& service)
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

        if (transport_ == Transport::DETECTING) {
            detectProtocol();
            // detectProtocol may call processWsHandshake which can fail and
            // call closeConnection — bail out if the connection is closed.
            if (phase_ == Phase::CLOSED)
                return;
            if (transport_ == Transport::DETECTING)
                return; // Need more data
        }

        if (transport_ == Transport::WS_HANDSHAKE) {
            processWsHandshake();
            return;
        }

        if (transport_ == Transport::WS_FRAMED) {
            processWsFrame();
            return;
        }

        // BOLT_RAW — existing handshake + chunked-transfer logic
        if (phase_ == Phase::HANDSHAKE) {
            if (read_buf_->length() >= 20) {
                processHandshake();
                if (phase_ == Phase::CLOSED || !read_buf_ || read_buf_->length() == 0)
                    return;
            } else if (read_buf_->length() >= 8) {
                processHandshake();
                if (phase_ == Phase::CLOSED || !read_buf_ || read_buf_->length() == 0)
                    return;
            } else {
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

    // Streaming PULL responses are already Bolt chunked, possibly containing
    // multiple data chunks before their final 0x0000 terminator. Validate the
    // whole stream instead of only checking the first chunk, otherwise large
    // messages split into multiple chunks get incorrectly wrapped again.
    auto isChunkedResponse = [](const std::vector<uint8_t>& data) {
        size_t pos = 0;
        bool saw_data_chunk = false;
        bool saw_terminator = false;
        while (pos < data.size()) {
            if (pos + 2 > data.size())
                return false;
            uint16_t chunk_size = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
            pos += 2;
            if (chunk_size == 0) {
                saw_terminator = true;
                continue;
            }
            if (chunk_size > BOLT_MAX_CHUNK_SIZE || pos + chunk_size > data.size())
                return false;
            saw_data_chunk = true;
            pos += chunk_size;
        }
        return saw_data_chunk && saw_terminator;
    };

    auto appendRawChunked = [](std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
        size_t offset = 0;
        do {
            size_t chunk_size = std::min<size_t>(BOLT_MAX_CHUNK_SIZE, data.size() - offset);
            if (chunk_size == 0)
                break;
            out.push_back(static_cast<uint8_t>((chunk_size >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(chunk_size & 0xFF));
            out.insert(out.end(), data.begin() + offset, data.begin() + offset + chunk_size);
            offset += chunk_size;
        } while (offset < data.size());
        out.push_back(0x00);
        out.push_back(0x00);
    };

    std::vector<uint8_t> body;
    if (isChunkedResponse(data)) {
        body = std::move(data);
    } else {
        appendRawChunked(body, data);
    }

    std::vector<uint8_t> out;
    if (transport_ == Transport::WS_FRAMED) {
        // Wrap chunked Bolt message in WebSocket binary frame
        out = websocket::buildFrame(body.data(), body.size(), websocket::Opcode::BINARY);
    } else {
        out = std::move(body);
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

// ==================== Transport detection & WebSocket ====================

void BoltConnection::detectProtocol() {
    auto data = read_buf_->data();
    auto len = read_buf_->length();

    if (len < 4)
        return; // Need more data

    // WebSocket upgrade: HTTP request starts with "GET "
    if (data[0] == 'G' && data[1] == 'E' && data[2] == 'T' && data[3] == ' ') {
        spdlog::info("[bolt] detected WebSocket upgrade request");
        transport_ = Transport::WS_HANDSHAKE;
        processWsHandshake();
        return;
    }

    // Bolt raw: magic bytes 0x60 0x60 0xB0 0x17
    transport_ = Transport::BOLT_RAW;
    spdlog::debug("[bolt] detected raw Bolt connection");
    // Fall through to normal handshake processing in readDataAvailable
}

void BoltConnection::processWsHandshake() {
    auto data = read_buf_->data();
    auto len = read_buf_->length();

    size_t header_end = websocket::findHeaderEnd(data, len);
    if (header_end == 0) {
        if (len > 65536) {
            spdlog::error("[bolt] WebSocket handshake header too large, closing");
            closeConnection();
        }
        return; // Need more data
    }

    std::string ws_key = websocket::parseHandshakeKey(data, header_end);
    if (ws_key.empty()) {
        spdlog::error("[bolt] WebSocket handshake: missing or invalid Sec-WebSocket-Key");
        closeConnection();
        return;
    }

    std::string response = websocket::buildHandshakeResponse(ws_key);
    auto buf = folly::IOBuf::copyBuffer(response.data(), response.size());
    socket_->writeChain(this, std::move(buf));

    // Consume the HTTP request bytes
    read_buf_->trimStart(header_end);
    if (read_buf_->length() == 0)
        read_buf_.reset();

    // WebSocket connections still perform the Bolt handshake inside WS frames
    transport_ = Transport::WS_FRAMED;
    // phase_ stays HANDSHAKE — first WS binary frame carries the Bolt magic+versions
    spdlog::info("[bolt] WebSocket upgrade complete, waiting for Bolt handshake");

    // Process any data that arrived after the HTTP headers
    if (read_buf_ && read_buf_->length() > 0)
        processWsFrame();
}

void BoltConnection::processWsFrame() {
    while (read_buf_ && read_buf_->length() >= 2) {
        auto data = read_buf_->data();
        auto len = read_buf_->length();

        websocket::FrameHeader hdr;
        if (!websocket::tryParseFrameHeader(data, len, hdr))
            return; // Incomplete header — wait for more data

        if (hdr.payload_len > websocket::kMaxFramePayload) {
            spdlog::error("[bolt] WebSocket frame payload {} exceeds max {}", hdr.payload_len,
                          websocket::kMaxFramePayload);
            closeConnection();
            return;
        }

        size_t frame_total = hdr.header_size + hdr.payload_len;
        if (len < frame_total)
            return; // Incomplete frame — wait for more data

        // We have a complete frame
        uint8_t* payload_start = const_cast<uint8_t*>(data) + hdr.header_size;

        // Unmask if needed (client → server MUST mask per RFC 6455)
        if (hdr.mask) {
            websocket::unmaskPayload(payload_start, hdr.payload_len, hdr.mask_key);
        }

        switch (hdr.opcode) {
        case websocket::Opcode::BINARY:
        case websocket::Opcode::TEXT: {
            if (!hdr.fin) {
                // Fragmented frame — accumulate and wait for continuation
                // TODO: support fragmented messages if needed
                spdlog::warn("[bolt] fragmented WebSocket frames not yet supported, closing");
                closeConnection();
                return;
            }

            if (phase_ == Phase::HANDSHAKE) {
                // First WS message is the Bolt handshake (magic + versions)
                auto response = session_.negotiateHandshake(payload_start, hdr.payload_len);
                if (response.empty()) {
                    spdlog::error("[bolt] WebSocket Bolt handshake negotiation failed");
                    closeConnection();
                    return;
                }
                phase_ = Phase::MESSAGES;
                // Handshake response (4 bytes version) is NOT chunked — wrap directly
                auto frame = websocket::buildFrame(response.data(), response.size(), websocket::Opcode::BINARY);
                auto buf = folly::IOBuf::copyBuffer(frame.data(), frame.size());
                socket_->writeChain(this, std::move(buf));
            } else {
                // Feed WS frame payload through chunked transfer decoder.
                // Neo4j Browser sends Bolt messages with chunked transfer
                // encoding inside WebSocket binary frames.
                auto saved_buf = std::move(read_buf_);
                read_buf_ = folly::IOBuf::copyBuffer(payload_start, hdr.payload_len);
                processMessage();
                read_buf_ = std::move(saved_buf);
                if (phase_ == Phase::CLOSED || !read_buf_)
                    return;
            }
            break;
        }
        case websocket::Opcode::PING: {
            auto pong = websocket::buildPongFrame(payload_start, hdr.payload_len);
            auto pong_buf = folly::IOBuf::copyBuffer(pong.data(), pong.size());
            socket_->writeChain(this, std::move(pong_buf));
            break;
        }
        case websocket::Opcode::PONG:
            // Ignore unsolicited pongs
            break;
        case websocket::Opcode::CLOSE: {
            auto close_frame = websocket::buildCloseFrame();
            auto close_buf = folly::IOBuf::copyBuffer(close_frame.data(), close_frame.size());
            socket_->writeChain(this, std::move(close_buf));
            closeConnection();
            return;
        }
        default:
            spdlog::debug("[bolt] ignoring WebSocket opcode {}", static_cast<int>(hdr.opcode));
            break;
        }

        read_buf_->trimStart(frame_total);
        if (read_buf_->length() == 0) {
            read_buf_.reset();
            return;
        }
    }
}

// ==================== BoltServer ====================

BoltServer::BoltServer(service::GraphService& service, uint16_t port) : service_(service), port_(port) {}

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
} // namespace service
} // namespace eugraph
