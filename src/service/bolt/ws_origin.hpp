#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {

/// True for browser origins whose host is loopback:
/// http(s)://localhost[:port], http(s)://127.0.0.1[:port], http(s)://[::1][:port].
/// The port is intentionally ignored: the console and the Vite dev server use
/// different ports.
bool isLoopbackOrigin(std::string_view origin);

/// WebSocket Origin policy for the Bolt listener.
///
/// Non-browser clients (official drivers) do not send an Origin header and are
/// handled before this policy is consulted. A browser request is accepted when
/// its Origin is loopback or exactly matches one of the configured entries.
class OriginPolicy {
public:
    explicit OriginPolicy(std::vector<std::string> extra_allowed = {});

    bool allows(std::string_view origin) const;

private:
    std::vector<std::string> extra_allowed_;
};

} // namespace bolt
} // namespace service
} // namespace eugraph
