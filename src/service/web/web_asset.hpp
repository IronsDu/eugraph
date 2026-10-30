#pragma once

#include <span>
#include <string_view>

namespace eugraph {
namespace service {
namespace web {

/// One immutable file embedded in the binary.
///
/// The generated implementation keeps the bytes in `.rodata` and exposes a
/// `std::string_view` over them; serving an asset never touches the filesystem.
struct WebAsset {
    std::string_view path;         ///< Absolute URL path, e.g. "/assets/app-abc.js".
    std::string_view content_type; ///< Value for the Content-Type header.
    std::string_view etag;         ///< Strong validator (unquoted hex digest).
    std::string_view body;         ///< Raw bytes.
    bool compressible = false;     ///< Text-ish asset eligible for startup gzip.
};

/// Assets generated from web/dist at build time.
std::span<const WebAsset> embeddedWebAssets();

/// Stable digest of the embedded manifest, used by /api/info and diagnostics.
std::string_view embeddedAssetsHash();

/// Sorted, read-only view of the embedded assets.
class WebAssetIndex {
public:
    explicit WebAssetIndex(std::span<const WebAsset> assets) : assets_(assets) {}

    /// Exact path lookup. Returns nullptr when the path is not embedded.
    const WebAsset* find(std::string_view path) const;

    /// The SPA entry point ("/index.html").
    const WebAsset* index() const;

    std::span<const WebAsset> assets() const {
        return assets_;
    }

private:
    std::span<const WebAsset> assets_;
};

} // namespace web
} // namespace service
} // namespace eugraph
