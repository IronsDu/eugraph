#pragma once

#include "service/web/web_asset.hpp"
#include "service/web/web_router.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace eugraph {
namespace service {
namespace web {

/// Embedded HTTP server for the web console.
///
/// The class is a thin adapter around Drogon: it owns the embedded asset table,
/// the startup gzip cache, and the Drogon application lifecycle. Drogon itself
/// is a process-wide singleton, so at most one instance can ever be started in
/// a process.
class WebConsoleServer {
public:
    explicit WebConsoleServer(WebConsoleConfig config);
    ~WebConsoleServer();

    WebConsoleServer(const WebConsoleServer&) = delete;
    WebConsoleServer& operator=(const WebConsoleServer&) = delete;

    /// Starts the server and blocks until the listener accepts connections.
    /// Returns false (with a human-readable error) on bind/startup failure.
    bool start(std::string* error);

    /// Stops the server and joins its thread. Drogon cannot be restarted after
    /// this call; a second start() in the same process is rejected.
    void stop();

    bool running() const {
        return running_.load();
    }

    uint16_t port() const {
        return config_.port;
    }

private:
    void buildGzipCache();
    bool probeListener() const;
    static bool portAvailable(const std::string& bind_address, uint16_t port, std::string* error);

    WebConsoleConfig config_;
    WebAssetIndex assets_;
    GzipCache gzip_cache_;
    WebRouter router_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace web
} // namespace service
} // namespace eugraph
