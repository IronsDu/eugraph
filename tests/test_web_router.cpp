#include "service/web/web_router.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace eugraph {
namespace service {
namespace web {
namespace {

constexpr std::string_view kIndexHtml = "<!doctype html><title>console</title>";
constexpr std::string_view kAppJs = "console.log('app');";
constexpr std::string_view kLicenses = "MIT";

const WebAsset kAssets[] = {
    {"/assets/app-abc.js", "text/javascript; charset=utf-8", "etag-app", kAppJs, true},
    {"/index.html", "text/html; charset=utf-8", "etag-index", kIndexHtml, true},
    {"/licenses.txt", "text/plain; charset=utf-8", "etag-licenses", kLicenses, true},
};

WebConsoleConfig testConfig() {
    WebConsoleConfig config;
    config.port = 7474;
    config.bolt_port = 7687;
    config.default_database = "default";
    config.version = "9.9.9-test";
    return config;
}

WebResponseSpec route(const WebRouter& router, std::string_view method, std::string_view path,
                      std::string_view host = "127.0.0.1:7474", std::string_view accept_encoding = "",
                      std::string_view if_none_match = "") {
    return router.route({method, path, host, accept_encoding, if_none_match});
}

std::string header(const WebResponseSpec& response, std::string_view name) {
    for (const auto& [key, value] : response.headers) {
        if (key == name)
            return value;
    }
    return {};
}

class WebRouterTest : public ::testing::Test {
protected:
    WebRouterTest()
        : index_(kAssets),
          gzip_cache_{
              {"/assets/app-abc.js", "GZIPPED"},
          },
          router_(index_, testConfig(), gzip_cache_) {}

    WebAssetIndex index_;
    GzipCache gzip_cache_;
    WebRouter router_;
};

TEST_F(WebRouterTest, ServesIndexAtRoot) {
    const auto response = route(router_, "GET", "/");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.content_type, "text/html; charset=utf-8");
    EXPECT_EQ(response.body, kIndexHtml);
    EXPECT_EQ(header(response, "Cache-Control"), "no-store");
    EXPECT_EQ(header(response, "X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(header(response, "X-Frame-Options"), "DENY");
}

TEST_F(WebRouterTest, ServesIndexExplicitly) {
    const auto response = route(router_, "GET", "/index.html");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, kIndexHtml);
}

TEST_F(WebRouterTest, ServesAssetAndImmutableCacheHeaders) {
    const auto response = route(router_, "GET", "/assets/app-abc.js");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, kAppJs);
    EXPECT_EQ(response.content_type, "text/javascript; charset=utf-8");
    EXPECT_EQ(header(response, "ETag"), "\"etag-app\"");
    EXPECT_EQ(header(response, "Vary"), "Accept-Encoding");
    EXPECT_EQ(header(response, "Cache-Control"), "public, max-age=31536000, immutable");
    EXPECT_TRUE(header(response, "Content-Encoding").empty());
}

TEST_F(WebRouterTest, ServesGzipVariantAndVariantEtag) {
    const auto response = route(router_, "GET", "/assets/app-abc.js", "127.0.0.1:7474", "gzip, deflate");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "GZIPPED");
    EXPECT_EQ(header(response, "Content-Encoding"), "gzip");
    EXPECT_EQ(header(response, "ETag"), "\"etag-app-gzip\"");
}

TEST_F(WebRouterTest, ConditionallyReturnsNotModifiedForRawVariant) {
    const auto response = route(router_, "GET", "/assets/app-abc.js", "127.0.0.1:7474", "", "\"etag-app\"");
    EXPECT_EQ(response.status, 304);
    EXPECT_TRUE(response.body.empty());
    EXPECT_TRUE(response.content_type.empty());
    EXPECT_EQ(header(response, "ETag"), "\"etag-app\"");
}

TEST_F(WebRouterTest, ConditionallyReturnsNotModifiedForGzipVariant) {
    const auto response = route(router_, "GET", "/assets/app-abc.js", "127.0.0.1:7474", "gzip", "W/\"etag-app-gzip\"");
    EXPECT_EQ(response.status, 304);
    EXPECT_EQ(header(response, "ETag"), "\"etag-app-gzip\"");
}

TEST_F(WebRouterTest, AcceptsIfNoneMatchList) {
    const auto response =
        route(router_, "GET", "/assets/app-abc.js", "127.0.0.1:7474", "", "\"other\", \"etag-app\", \"third\"");
    EXPECT_EQ(response.status, 304);
}

TEST_F(WebRouterTest, HeadUsesTheSameResponseSpec) {
    const auto response = route(router_, "HEAD", "/assets/app-abc.js");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, kAppJs);
    EXPECT_EQ(response.content_type, "text/javascript; charset=utf-8");
}

TEST_F(WebRouterTest, RejectsOtherMethods) {
    const auto response = route(router_, "POST", "/api/info");
    EXPECT_EQ(response.status, 405);
    EXPECT_EQ(header(response, "Allow"), "GET, HEAD");
}

TEST_F(WebRouterTest, Returns404ForUnknownAsset) {
    const auto response = route(router_, "GET", "/assets/missing.js");
    EXPECT_EQ(response.status, 404);
    EXPECT_EQ(header(response, "Cache-Control"), "no-store");
}

TEST_F(WebRouterTest, Returns404ForUnknownPaths) {
    EXPECT_EQ(route(router_, "GET", "/console").status, 404);
    EXPECT_EQ(route(router_, "GET", "/api/missing").status, 404);
    EXPECT_EQ(route(router_, "GET", "/assets/missing").status, 404);
}

TEST_F(WebRouterTest, RejectsTraversalAndEncodedPaths) {
    for (const std::string path :
         {"/../etc/passwd", "/assets/../../etc/passwd", "/%2e%2e/etc/passwd", "/assets/%2e%2e/app.js", "/a\\b"}) {
        const auto response = route(router_, "GET", path);
        EXPECT_EQ(response.status, 404) << path;
        EXPECT_TRUE(response.body.find("root:") == std::string::npos) << path;
    }
}

TEST_F(WebRouterTest, IgnoresQueryString) {
    const auto response = route(router_, "GET", "/assets/app-abc.js?v=1");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, kAppJs);
}

TEST_F(WebRouterTest, ServesInfoJson) {
    const auto response = route(router_, "GET", "/api/info");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.content_type, "application/json; charset=utf-8");
    EXPECT_EQ(header(response, "Cache-Control"), "no-store");

    const auto info = nlohmann::json::parse(response.body);
    EXPECT_EQ(info["product"], "EuGraph");
    EXPECT_EQ(info["version"], "9.9.9-test");
    EXPECT_EQ(info["bolt"]["port"], 7687);
    EXPECT_EQ(info["database"], "default");
    EXPECT_EQ(info["limits"]["graphNodes"], 2000);
    EXPECT_EQ(info["limits"]["graphEdges"], 5000);
    EXPECT_FALSE(info["assets"]["hash"].get<std::string>().empty());
}

TEST_F(WebRouterTest, ServesHealthz) {
    const auto response = route(router_, "GET", "/healthz");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok\n");
    EXPECT_EQ(header(response, "Cache-Control"), "no-store");
}

TEST_F(WebRouterTest, CspContainsWebSocketEndpointForHost) {
    const auto response = route(router_, "GET", "/", "localhost:7474");
    const std::string csp = header(response, "Content-Security-Policy");
    EXPECT_NE(csp.find("ws://localhost:7687"), std::string::npos);
    EXPECT_NE(csp.find("default-src 'self'"), std::string::npos);
    EXPECT_NE(csp.find("frame-ancestors 'none'"), std::string::npos);
}

TEST_F(WebRouterTest, CspFallsBackForInvalidHost) {
    const auto response = route(router_, "GET", "/", "bad<script>:7474");
    const std::string csp = header(response, "Content-Security-Policy");
    EXPECT_NE(csp.find("ws://localhost:7687"), std::string::npos);
    EXPECT_EQ(csp.find("bad<script>"), std::string::npos);
}

TEST_F(WebRouterTest, EmbeddedAssetsAreSortedAndContainIndex) {
    const auto assets = embeddedWebAssets();
    ASSERT_FALSE(assets.empty());
    EXPECT_EQ(embeddedWebAssets().front().path.rfind("/", 0), 0);

    const WebAssetIndex embedded(assets);
    ASSERT_NE(embedded.index(), nullptr);
    EXPECT_EQ(embedded.index()->path, "/index.html");
    EXPECT_FALSE(embeddedAssetsHash().empty());

    for (size_t i = 1; i < assets.size(); ++i)
        EXPECT_LT(assets[i - 1].path, assets[i].path) << i;
}

} // namespace
} // namespace web
} // namespace service
} // namespace eugraph
