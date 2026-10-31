#include "service/web/web_router.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace eugraph {
namespace service {
namespace web {

namespace {

constexpr std::string_view kIndexPath = "/index.html";

bool isControlChar(char c) {
    return static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7f;
}

bool isHostChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']';
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

} // namespace

WebRouter::WebRouter(const WebAssetIndex& assets, WebConsoleConfig config, const GzipCache& gzip_cache)
    : assets_(assets), config_(std::move(config)), gzip_cache_(gzip_cache) {}

WebResponseSpec WebRouter::route(const WebRequestView& request) const {
    if (request.method != "GET" && request.method != "HEAD")
        return makeMethodNotAllowedResponse(request.host);

    std::string_view path = stripQuery(request.path);
    if (!isValidPath(path))
        return makeNotFoundResponse(request.host);

    if (path == "/api/info")
        return makeInfoResponse();

    if (path == "/healthz") {
        WebResponseSpec response;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "ok\n";
        response.headers.emplace_back("Cache-Control", "no-store");
        addCommonHeaders(response, request.host);
        return response;
    }

    const WebAsset* asset = (path.empty() || path == "/") ? assets_.index() : assets_.find(path);
    if (asset == nullptr)
        return makeNotFoundResponse(request.host);

    return makeAssetResponse(*asset, request);
}

WebResponseSpec WebRouter::makeAssetResponse(const WebAsset& asset, const WebRequestView& request) const {
    const bool accepts_gzip = request.accept_encoding.find("gzip") != std::string_view::npos;
    const std::string* gzip_body = nullptr;
    if (accepts_gzip) {
        auto it = gzip_cache_.find(std::string(asset.path));
        if (it != gzip_cache_.end())
            gzip_body = &it->second;
    }

    std::string etag = "\"";
    etag.append(asset.etag);
    if (gzip_body != nullptr)
        etag.append("-gzip");
    etag.append("\"");

    const bool immutable = asset.path.rfind("/assets/", 0) == 0;
    const std::string cache_control = immutable ? "public, max-age=31536000, immutable" : "no-store";

    WebResponseSpec response;
    response.headers.emplace_back("ETag", etag);
    response.headers.emplace_back("Vary", "Accept-Encoding");
    response.headers.emplace_back("Cache-Control", cache_control);
    addCommonHeaders(response, request.host);

    if (etagMatches(request.if_none_match, etag)) {
        response.status = 304;
        return response;
    }

    response.content_type = std::string(asset.content_type);
    if (gzip_body != nullptr) {
        response.body = *gzip_body;
        response.headers.emplace_back("Content-Encoding", "gzip");
    } else {
        response.body = std::string(asset.body);
    }
    return response;
}

WebResponseSpec WebRouter::makeInfoResponse() const {
    nlohmann::json info;
    info["product"] = "EuGraph";
    info["version"] = config_.version;
    info["bolt"] = {{"port", config_.bolt_port}};
    info["database"] = config_.default_database;
    info["assets"] = {{"hash", std::string(embeddedAssetsHash())}};
    info["limits"] = {{"graphNodes", 2000}, {"graphEdges", 5000}};

    WebResponseSpec response;
    response.content_type = "application/json; charset=utf-8";
    response.body = info.dump();
    response.headers.emplace_back("Cache-Control", "no-store");
    addCommonHeaders(response, {});
    return response;
}

WebResponseSpec WebRouter::makeNotFoundResponse(std::string_view host) const {
    WebResponseSpec response;
    response.status = 404;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "not found\n";
    response.headers.emplace_back("Cache-Control", "no-store");
    addCommonHeaders(response, host);
    return response;
}

WebResponseSpec WebRouter::makeMethodNotAllowedResponse(std::string_view host) const {
    WebResponseSpec response;
    response.status = 405;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "method not allowed\n";
    response.headers.emplace_back("Allow", "GET, HEAD");
    response.headers.emplace_back("Cache-Control", "no-store");
    addCommonHeaders(response, host);
    return response;
}

void WebRouter::addCommonHeaders(WebResponseSpec& response, std::string_view host) const {
    response.headers.emplace_back("Content-Security-Policy", securityPolicy(host, config_.bolt_port));
    response.headers.emplace_back("X-Content-Type-Options", "nosniff");
    response.headers.emplace_back("X-Frame-Options", "DENY");
    response.headers.emplace_back("Referrer-Policy", "no-referrer");
}

std::string_view WebRouter::stripQuery(std::string_view path) {
    const auto query = path.find('?');
    if (query != std::string_view::npos)
        path = path.substr(0, query);
    return path;
}

bool WebRouter::isValidPath(std::string_view path) {
    if (path.empty() || path.front() != '/' || path.size() > 2048)
        return false;
    if (path.find("..") != std::string_view::npos || path.find('%') != std::string_view::npos ||
        path.find('\\') != std::string_view::npos)
        return false;
    for (char c : path) {
        if (isControlChar(c))
            return false;
    }
    return true;
}

std::string WebRouter::hostWithoutPort(std::string_view host) {
    host = trim(host);
    std::string_view value;
    if (!host.empty() && host.front() == '[') {
        const auto closing = host.find(']');
        if (closing == std::string_view::npos)
            return "localhost";
        value = host.substr(0, closing + 1);
    } else {
        const auto colon = host.rfind(':');
        value = colon == std::string_view::npos ? host : host.substr(0, colon);
        if (colon != std::string_view::npos) {
            const auto port = host.substr(colon + 1);
            if (port.empty() || !std::all_of(port.begin(), port.end(),
                                             [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                value = host;
        }
    }
    if (value.empty() || value.size() > 255)
        return "localhost";
    for (char c : value) {
        if (!isHostChar(c))
            return "localhost";
    }
    return std::string(value);
}

std::string WebRouter::securityPolicy(std::string_view host, uint16_t bolt_port) {
    const std::string safe_host = hostWithoutPort(host);
    const std::string port = std::to_string(bolt_port);
    std::string policy = "default-src 'self'; ";
    policy += "script-src 'self'; ";
    policy += "style-src 'self' 'unsafe-inline'; ";
    policy += "img-src 'self' data: blob:; ";
    policy += "font-src 'self' data:; ";
    policy += "connect-src 'self' ws://" + safe_host + ":" + port + " ws://localhost:" + port +
              " ws://127.0.0.1:" + port + " ws://[::1]:" + port + "; ";
    policy += "frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
    return policy;
}

bool WebRouter::etagMatches(std::string_view if_none_match, std::string_view etag) {
    if_none_match = trim(if_none_match);
    if (if_none_match.empty())
        return false;
    if (if_none_match == "*")
        return true;
    // If-None-Match may be a comma separated list, and validators may carry
    // the weak prefix. Compare the opaque parts only.
    size_t start = 0;
    while (start <= if_none_match.size()) {
        const size_t comma = if_none_match.find(',', start);
        std::string_view candidate =
            if_none_match.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        candidate = trim(candidate);
        if (candidate.rfind("W/", 0) == 0)
            candidate.remove_prefix(2);
        if (candidate == etag)
            return true;
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return false;
}

} // namespace web
} // namespace service
} // namespace eugraph
