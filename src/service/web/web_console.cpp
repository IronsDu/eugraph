#include "service/web/web_console.hpp"

#include <drogon/drogon.h>
#include <zlib.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <trantor/utils/Logger.h>

namespace eugraph {
namespace service {
namespace web {

namespace {

constexpr auto kStartupTimeout = std::chrono::seconds(10);
constexpr auto kProbeInterval = std::chrono::milliseconds(10);
constexpr int kProbeAttempts = 300;
constexpr size_t kMinGzipSize = 256;
constexpr uint16_t kMinPort = 1;

std::atomic<bool> g_consoleUsed{false};

std::string resolveBindAddress(const std::string& bind_address) {
    return bind_address == "localhost" ? "127.0.0.1" : bind_address;
}

std::string socketError(const std::string& bind_address, uint16_t port) {
    return "cannot bind " + bind_address + ":" + std::to_string(port) + ": " + std::strerror(errno);
}

std::string gzipCompress(std::string_view input) {
    if (input.empty() || input.size() > static_cast<size_t>(std::numeric_limits<uInt>::max()))
        return {};

    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};

    std::string output(deflateBound(&stream, static_cast<uLong>(input.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const int rc = deflate(&stream, Z_FINISH);
    const size_t written = stream.total_out;
    deflateEnd(&stream);

    if (rc != Z_STREAM_END)
        return {};
    output.resize(written);
    return output;
}

void setThreadName() {
#ifdef __linux__
    pthread_setname_np(pthread_self(), "eugraph-web");
#endif
}

} // namespace

WebConsoleServer::WebConsoleServer(WebConsoleConfig config)
    : config_(std::move(config)), assets_(embeddedWebAssets()), router_(assets_, config_, gzip_cache_) {}

WebConsoleServer::~WebConsoleServer() {
    stop();
}

bool WebConsoleServer::start(std::string* error) {
    auto fail = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    if (running_.load())
        return fail("web console is already running");

    bool expected = false;
    if (!g_consoleUsed.compare_exchange_strong(expected, true))
        return fail("web console has already been started in this process and cannot be restarted");

    if (config_.port < kMinPort)
        return fail("web console port must be greater than 0");

    if (assets_.index() == nullptr) {
        g_consoleUsed.store(false);
        return fail("embedded web assets are missing /index.html; rebuild with web/dist present");
    }

    const std::string bind_address = resolveBindAddress(config_.bind_address);
    if (!portAvailable(bind_address, config_.port, error)) {
        g_consoleUsed.store(false);
        return false;
    }

    buildGzipCache();

    drogon::app().disableSigtermHandling();
    drogon::app().setThreadNum(1);
    drogon::app().enableGzip(false);
    drogon::app().enableBrotli(false);
    drogon::app().setClientMaxBodySize(1024);
    drogon::app().setClientMaxMemoryBodySize(1024);
    drogon::app().setLogLevel(trantor::Logger::kWarn);
    drogon::app().setServerHeaderField("EuGraph");
    drogon::app().addListener(bind_address, config_.port, false);

    auto ready = std::make_shared<std::promise<void>>();
    std::future<void> ready_future = ready->get_future();

    drogon::app().registerBeginningAdvice([ready]() { ready->set_value(); });
    drogon::app().registerPreRoutingAdvice([this](const drogon::HttpRequestPtr& request,
                                                  drogon::AdviceCallback&& callback,
                                                  drogon::AdviceChainCallback&& /*chain*/) {
        std::string_view method = "OTHER";
        switch (request->method()) {
        case drogon::Get:
            method = "GET";
            break;
        case drogon::Head:
            method = "HEAD";
            break;
        default:
            break;
        }

        auto header = [&request](const char* lower, const char* canonical) -> std::string_view {
            const auto& value = request->getHeader(lower);
            if (!value.empty())
                return value;
            return request->getHeader(canonical);
        };

        WebRequestView view;
        view.method = method;
        view.path = request->path();
        view.host = header("host", "Host");
        view.accept_encoding = header("accept-encoding", "Accept-Encoding");
        view.if_none_match = header("if-none-match", "If-None-Match");

        WebResponseSpec spec = router_.route(view);
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(static_cast<drogon::HttpStatusCode>(spec.status));
        if (!spec.content_type.empty())
            response->setContentTypeString(spec.content_type);
        for (auto& [name, value] : spec.headers)
            response->addHeader(name, value);
        if (!spec.body.empty())
            response->setBody(std::move(spec.body));
        callback(response);
    });

    thread_ = std::thread([this]() {
        setThreadName();
        drogon::app().run();
    });

    if (ready_future.wait_for(kStartupTimeout) != std::future_status::ready) {
        stop();
        return fail("web console did not become ready within 10 seconds");
    }

    if (!probeListener()) {
        stop();
        return fail("web console listener did not accept connections on " + bind_address + ":" +
                    std::to_string(config_.port));
    }

    running_.store(true);
    return true;
}

void WebConsoleServer::stop() {
    if (thread_.joinable()) {
        drogon::app().quit();
        thread_.join();
    }
    running_.store(false);
}

void WebConsoleServer::buildGzipCache() {
    gzip_cache_.clear();
    for (const WebAsset& asset : assets_.assets()) {
        if (!asset.compressible || asset.body.size() < kMinGzipSize)
            continue;
        std::string compressed = gzipCompress(asset.body);
        if (compressed.empty() || compressed.size() + 32 >= asset.body.size())
            continue;
        gzip_cache_.emplace(std::string(asset.path), std::move(compressed));
    }
}

bool WebConsoleServer::probeListener() const {
    const std::string bind_address = resolveBindAddress(config_.bind_address);
    const std::string probe_address = bind_address == "0.0.0.0" ? "127.0.0.1" : bind_address;

    for (int attempt = 0; attempt < kProbeAttempts; ++attempt) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(config_.port);
            if (inet_pton(AF_INET, probe_address.c_str(), &address.sin_addr) == 1) {
                if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                    ::close(fd);
                    return true;
                }
            }
            ::close(fd);
        }
        std::this_thread::sleep_for(kProbeInterval);
    }
    return false;
}

bool WebConsoleServer::portAvailable(const std::string& bind_address, uint16_t port, std::string* error) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
        if (error != nullptr)
            *error = "unsupported web console bind address '" + bind_address + "' (IPv4 only)";
        return false;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (error != nullptr)
            *error = "cannot create socket: " + std::string(std::strerror(errno));
        return false;
    }

    const int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    const int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int saved_errno = errno;
    ::close(fd);
    if (rc != 0) {
        if (error != nullptr) {
            errno = saved_errno;
            *error = socketError(bind_address, port);
        }
        return false;
    }
    return true;
}

} // namespace web
} // namespace service
} // namespace eugraph
