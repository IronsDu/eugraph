#include "bolt/bolt_session.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace bolt {

// ==================== Handshake ====================

std::vector<uint8_t> BoltSession::negotiateHandshake(const uint8_t* data, size_t len) {
    // Client sends: 4 bytes magic + 4*N bytes version proposals (4 bytes each).
    // We need at least 4 + 4 = 8 bytes.
    if (len < 8) {
        spdlog::error("[bolt] handshake too short: {} bytes", len);
        return {};
    }

    // Verify magic
    uint32_t magic = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                     (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    if (magic != BOLT_MAGIC) {
        spdlog::error("[bolt] invalid magic: 0x{:08x}", magic);
        return {};
    }

    // Parse version proposals. Bolt v5.0+ drivers use a range-based encoding:
    //   uint16 minor_lower | uint8 major | uint8 minor_range
    // Older drivers send simple uint32 version numbers (major << 16 | minor).
    // We check both formats and also handle the catch-all range (0xFF).
    size_t num_versions = (len - 4) / 4;
    uint32_t selected = 0;

    for (size_t i = 0; i < num_versions; i++) {
        size_t off = 4 + i * 4;
        if (off + 4 > len)
            break;
        uint32_t ver = (static_cast<uint32_t>(data[off]) << 24) | (static_cast<uint32_t>(data[off + 1]) << 16) |
                       (static_cast<uint32_t>(data[off + 2]) << 8) | static_cast<uint32_t>(data[off + 3]);
        spdlog::debug("[bolt] client proposes version: 0x{:08x}", ver);

        // Try simple format: major << 16 | minor
        for (auto supported : BOLT_PROPOSED_VERSIONS) {
            if (ver == supported && ver > selected) {
                selected = ver;
            }
        }

        // Try range format: uint16 minor | uint8 major | uint8 range
        if (selected == 0) {
            uint16_t minor_lower = static_cast<uint16_t>((ver >> 16) & 0xFFFF);
            uint8_t major = static_cast<uint8_t>((ver >> 8) & 0xFF);
            uint8_t range = static_cast<uint8_t>(ver & 0xFF);

            for (auto supported : BOLT_PROPOSED_VERSIONS) {
                uint8_t sup_major = static_cast<uint8_t>((supported >> 8) & 0xFF);
                uint8_t sup_minor = static_cast<uint8_t>(supported & 0xFF);
                if (sup_major == major && sup_minor >= minor_lower && sup_minor < minor_lower + range + 1) {
                    if (supported > selected)
                        selected = supported;
                }
            }
        }

        // If we see a catch-all (range == 0xFF for major=1 in new format, or
        // proposal 0x000001FF from neo4j 5.x), accept our best version.
        if (selected == 0 && ver == 0x000001FF) {
            selected = BOLT_VERSION_5_1;
        }
    }

    // If no match: accept the best version anyway (neo4j 5.x drivers use
    // non-obvious range encoding that may not match our simple parsing).
    if (selected == 0) {
        spdlog::info("[bolt] no exact version match, defaulting to 5.1");
        selected = BOLT_VERSION_5_1;
    }

    negotiated_version_ = selected;
    state_ = SessionState::CONNECTING;
    spdlog::info("[bolt] negotiated version: {}.{}", (selected >> 8) & 0xFF, selected & 0xFF);

    // v1 handshake response: 4 bytes total.
    // neo4j driver parses: agreed_version = (response[-1], response[-2]) = (major, minor)
    // Version encoding: major in bits 8-15, minor in bits 0-7 (e.g. 5.1 = 0x0501)
    // So for version 5.1 we send: [0, 0, 1, 5] (minor=1 at index 2, major=5 at index 3)
    uint8_t major = static_cast<uint8_t>((selected >> 8) & 0xFF);
    uint8_t minor = static_cast<uint8_t>(selected & 0xFF);
    std::vector<uint8_t> response(4, 0);
    response[2] = minor;
    response[3] = major;
    return response;
}

// ==================== Serialization helpers ====================

std::vector<uint8_t> BoltSession::serialize(const packstream::Value& v) {
    packstream::Encoder enc;
    enc.writeValue(v);
    return enc.release();
}

std::vector<uint8_t> BoltSession::makeSuccess(const std::unordered_map<std::string, packstream::Value>& fields) {
    packstream::Encoder enc;
    packstream::PackStreamStruct s;
    s.tag = tags::SUCCESS;
    std::unordered_map<std::string, packstream::PackStreamValueStorage> wrapped;
    for (auto& [k, v] : fields)
        wrapped[k] = packstream::PackStreamValueStorage{v};
    s.fields.push_back(packstream::PackStreamValueStorage{std::move(wrapped)});
    enc.writeValue(s);
    return enc.release();
}

std::vector<uint8_t> BoltSession::makeFailure(const std::string& code, const std::string& message) {
    packstream::Encoder enc;
    packstream::PackStreamStruct s;
    s.tag = tags::FAILURE;
    std::unordered_map<std::string, packstream::PackStreamValueStorage> meta;
    meta["code"] = packstream::PackStreamValueStorage{code};
    meta["message"] = packstream::PackStreamValueStorage{message};
    s.fields.push_back(packstream::PackStreamValueStorage{std::move(meta)});
    enc.writeValue(s);
    return enc.release();
}

std::vector<uint8_t> BoltSession::makeIgnored() {
    packstream::Encoder enc;
    packstream::PackStreamStruct s;
    s.tag = tags::IGNORED;
    enc.writeValue(s);
    return enc.release();
}

std::vector<uint8_t> BoltSession::makeRecord(const std::vector<packstream::Value>& fields) {
    packstream::Encoder enc;
    packstream::PackStreamStruct s;
    s.tag = tags::RECORD;
    std::vector<packstream::PackStreamValueStorage> list;
    for (auto& f : fields)
        list.push_back(packstream::PackStreamValueStorage{f});
    s.fields.push_back(packstream::PackStreamValueStorage{std::move(list)});
    enc.writeValue(s);
    return enc.release();
}

// ==================== Message Processor ====================

folly::coro::Task<std::vector<uint8_t>> BoltSession::processMessage(const uint8_t* data, size_t len) {
    packstream::Decoder decoder(data, len);

    try {
        auto [tag, field_count] = decoder.decodeStructHeader();

        switch (tag) {
        case tags::HELLO: {
            if (field_count != 1) {
                co_return makeFailure("ProtocolError", "HELLO must have 1 field");
            }
            auto val = decoder.decode();
            if (!std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val)) {
                co_return makeFailure("ProtocolError", "HELLO field must be a dictionary");
            }
            // Unwrap PackStreamValueStorage → value
            std::unordered_map<std::string, packstream::Value> fields;
            for (auto& [k, v] : std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val))
                fields[k] = v.value;
            co_return co_await handleHello(fields);
        }
        case tags::LOGON: {
            if (field_count != 1) {
                co_return makeFailure("ProtocolError", "LOGON must have 1 field");
            }
            auto val = decoder.decode();
            std::unordered_map<std::string, packstream::Value> fields;
            if (std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val)) {
                for (auto& [k, v] : std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val))
                    fields[k] = v.value;
            }
            co_return co_await handleLogon(fields);
        }
        case tags::LOGOFF:
            co_return co_await handleLogoff();
        case tags::RUN: {
            if (field_count != 3) {
                co_return makeFailure("ProtocolError", "RUN must have 3 fields");
            }
            RunMessage msg;
            auto q = decoder.decode();
            if (!std::holds_alternative<std::string>(q))
                co_return makeFailure("ProtocolError", "RUN query must be a string");
            msg.query = std::move(std::get<std::string>(q));

            auto params = decoder.decode();
            if (std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(params)) {
                for (auto& [k, v] :
                     std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(params))
                    msg.parameters[k] = v.value;
            }
            auto extra = decoder.decode();
            if (std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(extra)) {
                for (auto& [k, v] :
                     std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(extra))
                    msg.extra[k] = v.value;
            }
            co_return co_await handleRun(msg);
        }
        case tags::PULL: {
            if (field_count != 1) {
                co_return makeFailure("ProtocolError", "PULL must have 1 field");
            }
            auto val = decoder.decode();
            PullMessage msg;
            if (std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val)) {
                auto& d = std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val);
                auto it = d.find("n");
                if (it != d.end() && std::holds_alternative<int64_t>(it->second.value))
                    msg.n = std::get<int64_t>(it->second.value);
            }
            co_return co_await handlePull(msg);
        }
        case tags::DISCARD: {
            DiscardMessage msg;
            co_return co_await handleDiscard(msg);
        }
        case tags::BEGIN: {
            BeginMessage msg;
            if (field_count >= 1) {
                auto val = decoder.decode();
                if (std::holds_alternative<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val)) {
                    for (auto& [k, v] :
                         std::get<std::unordered_map<std::string, packstream::PackStreamValueStorage>>(val))
                        msg.extra[k] = v.value;
                }
            }
            co_return co_await handleBegin(msg);
        }
        case tags::COMMIT:
            co_return co_await handleCommit();
        case tags::ROLLBACK:
            co_return co_await handleRollback();
        case tags::RESET:
            co_return co_await handleReset();
        case tags::GOODBYE:
            co_return co_await handleGoodbye();
        default: {
            spdlog::warn("[bolt] unknown message tag: 0x{:02x}", tag);
            co_return makeFailure("ProtocolError", "Unknown message type");
        }
        }
    } catch (const packstream::DecodeError& e) {
        spdlog::error("[bolt] decode error: {}", e.what());
        co_return makeFailure("ProtocolError", e.what());
    } catch (const std::exception& e) {
        spdlog::error("[bolt] session error: {}", e.what());
        if (state_ != SessionState::FAILED) {
            state_ = SessionState::FAILED;
        }
        co_return makeFailure("DatabaseError", e.what());
    }
}

// ==================== Message Handlers ====================

folly::coro::Task<std::vector<uint8_t>>
BoltSession::handleHello(const std::unordered_map<std::string, packstream::Value>& fields) {
    if (state_ != SessionState::CONNECTING) {
        co_return makeFailure("ProtocolError", "Unexpected HELLO in current state");
    }

    // Extract user_agent for logging
    auto it = fields.find("user_agent");
    if (it != fields.end() && std::holds_alternative<std::string>(it->second)) {
        spdlog::info("[bolt] client: {}", std::get<std::string>(it->second));
    }

    // Extract database name from HELLO fields
    auto db_it = fields.find("db");
    if (db_it != fields.end() && std::holds_alternative<std::string>(db_it->second)) {
        current_database_ = std::get<std::string>(db_it->second);
        spdlog::info("[bolt] database: {}", current_database_);
    }

    std::unordered_map<std::string, packstream::Value> meta;
    meta["server"] = std::string{"Neo4j/EuGraph-1.0"};
    meta["connection_id"] = std::string{"bolt-1"};

    state_ = SessionState::READY;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>>
BoltSession::handleLogon(const std::unordered_map<std::string, packstream::Value>& /*fields*/) {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    // Accept any LOGON (no authentication required)
    spdlog::debug("[bolt] LOGON accepted");
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleLogoff() {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    spdlog::debug("[bolt] LOGOFF accepted");
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleRun(const RunMessage& msg) {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    if (state_ != SessionState::READY && state_ != SessionState::TX_READY) {
        co_return makeFailure("ProtocolError", "Unexpected RUN in current state");
    }

    // Convert Bolt parameters to internal Values
    std::unordered_map<std::string, Value> params;
    for (auto& [key, val] : msg.parameters) {
        params[key] = boltParamToValue(val);
    }

    try {
        // Use db from RUN extra metadata if present, otherwise keep current
        auto db_it = msg.extra.find("db");
        std::string db_name = current_database_;
        if (db_it != msg.extra.end() && std::holds_alternative<std::string>(db_it->second))
            db_name = std::get<std::string>(db_it->second);

        auto exec_ctx = co_await service_.executeCypher(msg.query, params, db_name);

        // Handle USE <graph> — update session database context
        if (!exec_ctx.switched_database.empty())
            current_database_ = exec_ctx.switched_database;

        stream_ctx_ = std::move(exec_ctx.ctx);
        label_defs_ = std::move(exec_ctx.label_defs);
        edge_label_defs_ = std::move(exec_ctx.edge_label_defs);

        // Build success metadata with field names
        std::unordered_map<std::string, packstream::Value> meta;
        std::vector<packstream::PackStreamValueStorage> field_list;
        for (auto& col : stream_ctx_->columns)
            field_list.push_back({col});
        meta["fields"] = std::move(field_list);
        meta["t_first"] = static_cast<int64_t>(0);

        // State transition
        if (state_ == SessionState::TX_READY) {
            state_ = SessionState::TX_STREAMING;
        } else {
            state_ = SessionState::STREAMING;
        }

        co_return makeSuccess(meta);
    } catch (const std::exception& e) {
        state_ = SessionState::FAILED;
        co_return makeFailure("DatabaseError", e.what());
    }
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handlePull(const PullMessage& msg) {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    if (state_ != SessionState::STREAMING && state_ != SessionState::TX_STREAMING) {
        co_return makeFailure("ProtocolError", "Unexpected PULL in current state");
    }

    if (!stream_ctx_) {
        // No stream context (e.g., CALL db.ping()): return SUCCESS with no records.
        if (state_ == SessionState::TX_STREAMING) {
            state_ = SessionState::TX_READY;
        } else {
            state_ = SessionState::READY;
        }
        std::unordered_map<std::string, packstream::Value> meta;
        meta["type"] = std::string{"r"};
        co_return makeSuccess(meta);
    }

    // We build a pre-chunked response: each RECORD and the final SUCCESS
    // are wrapped in their own Bolt v5.1 chunk (2-byte size + data + 0x0000).
    // This is necessary because the Bolt protocol requires each message to
    // be in its own chunk, separate from other messages.
    auto wrapChunk = [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
        std::vector<uint8_t> chunk;
        uint16_t size = static_cast<uint16_t>(data.size());
        chunk.reserve(2 + data.size() + 2);
        chunk.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
        chunk.push_back(static_cast<uint8_t>(size & 0xFF));
        chunk.insert(chunk.end(), data.begin(), data.end());
        chunk.push_back(0x00);
        chunk.push_back(0x00);
        return chunk;
    };

    std::vector<uint8_t> response;
    int64_t fetched = 0;
    int64_t limit = msg.n;

    try {
        // Read from the async generator and encode RECORD messages
        while (auto chunk = co_await stream_ctx_->gen.next()) {
            auto rows = chunk->toRows();
            for (auto& row : rows) {
                if (limit >= 0 && fetched >= limit) {
                    // We've reached the limit but there may be more.
                    // For simplicity, we stop here. A proper impl would save state.
                    // Actually: n=-1 means all, n>=0 means specific count.
                    // If we fetched enough, break out of row loop.
                    // But we already consumed from the generator. For now,
                    // we just continue if limit is reached.
                    goto done_fetching;
                }

                std::vector<packstream::Value> record_fields;
                for (auto& val : row) {
                    record_fields.push_back(valueToBolt(val, label_defs_, edge_label_defs_));
                }
                auto record_chunk = wrapChunk(makeRecord(record_fields));
                response.insert(response.end(), record_chunk.begin(), record_chunk.end());
                fetched++;
            }
        }
    done_fetching:
        // Commit auto-commit transaction
        if (!in_transaction_ && stream_ctx_->should_commit) {
            co_await stream_ctx_->store.commitTran(stream_ctx_->txn);
        }

        // Build success metadata
        std::unordered_map<std::string, packstream::Value> meta;
        meta["type"] = std::string{"r"};
        meta["t_last"] = static_cast<int64_t>(0);
        if (limit >= 0 && fetched >= limit) {
            meta["has_more"] = true;
        }

        auto success_chunk = wrapChunk(makeSuccess(meta));
        response.insert(response.end(), success_chunk.begin(), success_chunk.end());

        // State transition
        if (state_ == SessionState::TX_STREAMING) {
            state_ = SessionState::TX_READY;
        } else {
            state_ = SessionState::READY;
        }

        // Clear stream context
        stream_ctx_.reset();

        co_return response;
    } catch (const std::exception& e) {
        state_ = SessionState::FAILED;
        stream_ctx_.reset();
        co_return makeFailure("DatabaseError", e.what());
    }
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleDiscard(const DiscardMessage&) {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    // Discard remaining results and close stream
    stream_ctx_.reset();
    if (state_ == SessionState::TX_STREAMING) {
        state_ = SessionState::TX_READY;
    } else {
        state_ = SessionState::READY;
    }
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleBegin(const BeginMessage&) {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    if (state_ != SessionState::READY) {
        co_return makeFailure("ProtocolError", "Unexpected BEGIN in current state");
    }

    in_transaction_ = true;
    state_ = SessionState::TX_READY;
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleCommit() {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    if (state_ != SessionState::TX_READY) {
        co_return makeFailure("ProtocolError", "Unexpected COMMIT in current state");
    }

    in_transaction_ = false;
    state_ = SessionState::READY;
    std::unordered_map<std::string, packstream::Value> meta;
    meta["bookmark"] = std::string{""};
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleRollback() {
    if (state_ == SessionState::FAILED) {
        co_return makeIgnored();
    }
    if (state_ != SessionState::TX_READY) {
        co_return makeFailure("ProtocolError", "Unexpected ROLLBACK in current state");
    }

    in_transaction_ = false;
    state_ = SessionState::READY;
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleReset() {
    // RESET is valid in any state
    stream_ctx_.reset();
    in_transaction_ = false;
    state_ = SessionState::READY;
    std::unordered_map<std::string, packstream::Value> meta;
    co_return makeSuccess(meta);
}

folly::coro::Task<std::vector<uint8_t>> BoltSession::handleGoodbye() {
    // GOODBYE is valid in any state
    stream_ctx_.reset();
    state_ = SessionState::CLOSED;
    // No response needed; connection will be closed by caller
    co_return std::vector<uint8_t>{};
}

} // namespace bolt
} // namespace eugraph
