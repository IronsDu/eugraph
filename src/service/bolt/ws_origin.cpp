#include "service/bolt/ws_origin.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace eugraph {
namespace service {
namespace bolt {

namespace {

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (asciiLower(lhs[i]) != asciiLower(rhs[i]))
            return false;
    }
    return true;
}

std::string_view trimTrailingSlash(std::string_view value) {
    while (!value.empty() && value.back() == '/')
        value.remove_suffix(1);
    return value;
}

} // namespace

bool isLoopbackOrigin(std::string_view origin) {
    if (origin.rfind("http://", 0) == 0)
        origin.remove_prefix(7);
    else if (origin.rfind("https://", 0) == 0)
        origin.remove_prefix(8);
    else
        return false;

    const size_t authority_end = origin.find_first_of("/?#");
    std::string_view authority = origin.substr(0, authority_end);
    if (authority.empty())
        return false;

    // Origin does not normally contain userinfo, but tolerate it rather than
    // comparing the wrong part.
    const size_t at = authority.rfind('@');
    if (at != std::string_view::npos)
        authority.remove_prefix(at + 1);

    std::string_view host;
    if (!authority.empty() && authority.front() == '[') {
        const size_t closing = authority.find(']');
        if (closing == std::string_view::npos)
            return false;
        host = authority.substr(0, closing + 1);
    } else {
        const size_t colon = authority.rfind(':');
        host = colon == std::string_view::npos ? authority : authority.substr(0, colon);
    }

    return equalsIgnoreCase(host, "localhost") || equalsIgnoreCase(host, "127.0.0.1") ||
           equalsIgnoreCase(host, "[::1]");
}

OriginPolicy::OriginPolicy(std::vector<std::string> extra_allowed) : extra_allowed_(std::move(extra_allowed)) {
    for (std::string& origin : extra_allowed_)
        origin = std::string(trimTrailingSlash(origin));
}

bool OriginPolicy::allows(std::string_view origin) const {
    if (origin.empty())
        return false;
    if (isLoopbackOrigin(origin))
        return true;
    const std::string_view normalized = trimTrailingSlash(origin);
    return std::find(extra_allowed_.begin(), extra_allowed_.end(), normalized) != extra_allowed_.end();
}

} // namespace bolt
} // namespace service
} // namespace eugraph
