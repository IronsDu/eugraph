#include "service/web/web_asset.hpp"

#include <algorithm>

namespace eugraph {
namespace service {
namespace web {

const WebAsset* WebAssetIndex::find(std::string_view path) const {
    auto it = std::lower_bound(assets_.begin(), assets_.end(), path,
                               [](const WebAsset& asset, std::string_view value) { return asset.path < value; });
    if (it == assets_.end() || it->path != path)
        return nullptr;
    return &*it;
}

const WebAsset* WebAssetIndex::index() const {
    return find("/index.html");
}

} // namespace web
} // namespace service
} // namespace eugraph
