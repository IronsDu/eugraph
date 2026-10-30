#pragma once

#include "service/web/web_asset.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace service {
namespace web {

/// Runtime configuration of the embedded web console.
struct WebConsoleConfig {
    uint16_t port = 7475;
    std::string bind_address = "127.0.0.1";
    uint16_t bolt_port = 7687;
    std::string default_database = "default";
    std::string version = "0.1.0";
};

/// Protocol-independent view of one HTTP request. Only the fields the router
/// needs are exposed, which keeps the routing logic unit-testable without a
/// real socket.
struct WebRequestView {
    std::string_view method;          ///< "GET", "HEAD", or another method.
    std::string_view path;            ///< Path only (query string already removed).
    std::string_view host;            ///< Host header (used to build the CSP).
    std::string_view accept_encoding; ///< Accept-Encoding header.
    std::string_view if_none_match;   ///< If-None-Match header.
};

struct WebResponseSpec {
    int status = 200;
    std::string content_type;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

/// Pre-compressed assets keyed by their URL path. Built once at startup.
using GzipCache = std::unordered_map<std::string, std::string>;

/// Pure routing layer: path + method + a few headers in, response out.
/// It never touches the filesystem or a socket, so every branch is unit-tested.
class WebRouter {
public:
    WebRouter(const WebAssetIndex& assets, WebConsoleConfig config, const GzipCache& gzip_cache);

    WebResponseSpec route(const WebRequestView& request) const;

private:
    WebResponseSpec makeAssetResponse(const WebAsset& asset, const WebRequestView& request) const;
    WebResponseSpec makeInfoResponse() const;
    WebResponseSpec makeNotFoundResponse(std::string_view host) const;
    WebResponseSpec makeMethodNotAllowedResponse(std::string_view host) const;

    void addCommonHeaders(WebResponseSpec& response, std::string_view host) const;
    static std::string_view stripQuery(std::string_view path);
    static bool isValidPath(std::string_view path);
    static std::string hostWithoutPort(std::string_view host);
    static std::string securityPolicy(std::string_view host, uint16_t bolt_port);
    static bool etagMatches(std::string_view if_none_match, std::string_view etag);

    const WebAssetIndex& assets_;
    WebConsoleConfig config_;
    const GzipCache& gzip_cache_;
};

} // namespace web
} // namespace service
} // namespace eugraph
