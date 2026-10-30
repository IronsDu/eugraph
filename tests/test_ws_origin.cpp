#include "service/bolt/websocket.hpp"
#include "service/bolt/ws_origin.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {
namespace {

std::string upgradeRequest(std::string_view extra_headers = "", bool include_origin = true,
                           std::string_view method = "GET") {
    std::string request = std::string(method) + " / HTTP/1.1\r\n";
    request += "Host: localhost:7687\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    if (include_origin)
        request += "Origin: http://localhost:7474\r\n";
    request += extra_headers;
    request += "\r\n";
    return request;
}

websocket::HandshakeRequest parse(std::string_view request) {
    websocket::HandshakeRequest parsed;
    EXPECT_TRUE(
        websocket::parseHandshakeRequest(reinterpret_cast<const uint8_t*>(request.data()), request.size(), parsed));
    return parsed;
}

TEST(WebSocketHandshakeTest, ParsesPathKeyAndOrigin) {
    const auto parsed = parse(upgradeRequest());
    EXPECT_EQ(parsed.path, "/");
    EXPECT_EQ(parsed.key, "dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_TRUE(parsed.has_origin);
    EXPECT_EQ(parsed.origin, "http://localhost:7474");
}

TEST(WebSocketHandshakeTest, ParsesLowercaseHeaders) {
    const std::string request = "GET /console HTTP/1.1\r\n"
                                "host: localhost:7687\r\n"
                                "upgrade: websocket\r\n"
                                "connection: upgrade\r\n"
                                "sec-websocket-key:   abcdef  \r\n"
                                "origin: http://127.0.0.1:5173\r\n"
                                "\r\n";
    const auto parsed = parse(request);
    EXPECT_EQ(parsed.path, "/console");
    EXPECT_EQ(parsed.key, "abcdef");
    EXPECT_TRUE(parsed.has_origin);
    EXPECT_EQ(parsed.origin, "http://127.0.0.1:5173");
}

TEST(WebSocketHandshakeTest, MissingOriginIsReported) {
    const auto parsed = parse(upgradeRequest("", false));
    EXPECT_FALSE(parsed.has_origin);
    EXPECT_TRUE(parsed.origin.empty());
}

TEST(WebSocketHandshakeTest, RejectsNonUpgradeRequest) {
    websocket::HandshakeRequest parsed;
    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    EXPECT_FALSE(
        websocket::parseHandshakeRequest(reinterpret_cast<const uint8_t*>(request.data()), request.size(), parsed));
}

TEST(WebSocketHandshakeTest, RejectsNonGetMethod) {
    const std::string request = upgradeRequest("", true, "POST");
    websocket::HandshakeRequest parsed;
    EXPECT_FALSE(
        websocket::parseHandshakeRequest(reinterpret_cast<const uint8_t*>(request.data()), request.size(), parsed));
}

TEST(WebSocketHandshakeTest, RejectsMissingKey) {
    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n\r\n";
    websocket::HandshakeRequest parsed;
    EXPECT_FALSE(
        websocket::parseHandshakeRequest(reinterpret_cast<const uint8_t*>(request.data()), request.size(), parsed));
}

TEST(WebSocketHandshakeTest, ForbiddenResponseIsWellFormed) {
    const std::string response = websocket::buildForbiddenResponse();
    EXPECT_EQ(response.rfind("HTTP/1.1 403 Forbidden\r\n", 0), 0);
    EXPECT_NE(response.find("Content-Length: 0\r\n"), std::string::npos);
    EXPECT_NE(response.find("Connection: close\r\n"), std::string::npos);
    EXPECT_EQ(response.rfind("\r\n\r\n"), response.size() - 4);
}

TEST(WebSocketOriginTest, RecognisesLoopbackOrigins) {
    for (const std::string_view origin : {
             "http://localhost:7474",
             "https://localhost:7474",
             "http://localhost",
             "http://127.0.0.1:7474",
             "https://127.0.0.1",
             "http://[::1]:7474",
             "https://[::1]",
             "http://LOCALHOST:5173",
         }) {
        EXPECT_TRUE(isLoopbackOrigin(origin)) << std::string(origin);
    }
}

TEST(WebSocketOriginTest, RejectsNonLoopbackOrigins) {
    for (const std::string_view origin : {
             "http://evil.com",
             "https://evil.com:7474",
             "http://localhost.evil.com:7474",
             "http://127.0.0.1.evil.com",
             "http://[::2]:7474",
             "null",
             "file:///tmp/x",
             "",
         }) {
        EXPECT_FALSE(isLoopbackOrigin(origin)) << std::string(origin);
    }
}

TEST(WebSocketOriginTest, PolicyAcceptsLoopbackAndConfiguredOrigins) {
    OriginPolicy policy({"https://console.example.com:7443/", "http://192.168.1.5:7474"});
    EXPECT_TRUE(policy.allows("http://localhost:7474"));
    EXPECT_TRUE(policy.allows("http://127.0.0.1:5173"));
    EXPECT_TRUE(policy.allows("https://console.example.com:7443"));
    EXPECT_TRUE(policy.allows("http://192.168.1.5:7474"));
    EXPECT_FALSE(policy.allows("https://evil.com"));
    EXPECT_FALSE(policy.allows("http://192.168.1.5:7475"));
    EXPECT_FALSE(policy.allows(""));
}

TEST(WebSocketOriginTest, PolicyWithoutExtraOriginsIsLoopbackOnly) {
    OriginPolicy policy;
    EXPECT_TRUE(policy.allows("http://localhost:7474"));
    EXPECT_FALSE(policy.allows("http://192.168.1.5:7474"));
}

} // namespace
} // namespace bolt
} // namespace service
} // namespace eugraph
