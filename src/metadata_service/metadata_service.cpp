#include "metadata_service/metadata_service.hpp"
#include "common/types/constants.hpp"
#include "metadata_service/metadata_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Lifecycle ====================

folly::coro::Task<bool> MetadataServiceImpl::open(IGraphStore& store) {
    store_ = &store;

    // Load all metadata from KV into cache
    // Scan M|label:* entries
    store_->metadataScan("M|label:", [&](std::string_view key, std::string_view value) {
        // key = "M|label:{name}", extract name
        std::string name(key.substr(8)); // skip "M|label:"
        auto def = MetadataCodec::decodeLabelDef(value);
        cache_.label_by_name[name] = def;
        cache_.label_by_id[def.id] = def;
        if (def.id >= cache_.next_label_id)
            cache_.next_label_id = static_cast<LabelId>(def.id + 1);
        return true;
    });

    // Scan M|edge_label:* entries
    store_->metadataScan("M|edge_label:", [&](std::string_view key, std::string_view value) {
        std::string name(key.substr(13)); // skip "M|edge_label:"
        auto def = MetadataCodec::decodeEdgeLabelDef(value);
        cache_.edge_label_by_name[name] = def;
        cache_.edge_label_by_id[def.id] = def;
        if (def.id >= cache_.next_edge_label_id)
            cache_.next_edge_label_id = static_cast<EdgeLabelId>(def.id + 1);
        return true;
    });

    // Load ID counters
    auto ids_data = store_->metadataGet("M|next_ids");
    if (ids_data) {
        MetadataCodec::decodeNextIds(*ids_data, cache_.next_vertex_id, cache_.next_edge_id, cache_.next_label_id,
                                     cache_.next_edge_label_id);
    }

    spdlog::info("MetadataService opened: {} labels, {} edge labels, next_vid={}, next_eid={}",
                 cache_.label_by_name.size(), cache_.edge_label_by_name.size(), cache_.next_vertex_id,
                 cache_.next_edge_id);

    co_return true;
}

folly::coro::Task<void> MetadataServiceImpl::close() {
    store_ = nullptr;
    cache_ = MetadataCache{};
    co_return;
}

// ==================== Label management ====================

folly::coro::Task<LabelId> MetadataServiceImpl::createLabel(const std::string& name,
                                                            const std::vector<PropertyDef>& properties) {
    if (cache_.label_by_name.count(name)) {
        spdlog::error("Label '{}' already exists", name);
        co_return INVALID_LABEL_ID;
    }

    LabelId id = cache_.next_label_id++;

    // Build LabelDef with assigned prop_ids
    LabelDef def;
    def.id = id;
    def.name = name;
    uint16_t prop_id = 1;
    for (const auto& p : properties) {
        PropertyDef pd = p;
        pd.id = prop_id++;
        def.properties.push_back(std::move(pd));
    }

    // Persist
    auto encoded = MetadataCodec::encodeLabelDef(def);
    std::string name_key = std::string("M|label:") + name;
    std::string id_key = std::string("M|label_id:") + std::to_string(id);

    store_->metadataPut(name_key, encoded);
    store_->metadataPut(id_key, encoded);
    saveNextIds();

    // Create storage tables
    if (!store_->createLabel(id)) {
        spdlog::error("Failed to create storage tables for label '{}' (id={})", name, id);
        co_return INVALID_LABEL_ID;
    }

    // Update cache
    cache_.label_by_name[name] = def;
    cache_.label_by_id[id] = def;

    spdlog::info("Created label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

folly::coro::Task<std::optional<LabelId>> MetadataServiceImpl::getLabelId(const std::string& name) {
    auto it = cache_.label_by_name.find(name);
    if (it != cache_.label_by_name.end())
        co_return it->second.id;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<std::string>> MetadataServiceImpl::getLabelName(LabelId id) {
    auto it = cache_.label_by_id.find(id);
    if (it != cache_.label_by_id.end())
        co_return it->second.name;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<LabelDef>> MetadataServiceImpl::getLabelDef(const std::string& name) {
    auto it = cache_.label_by_name.find(name);
    if (it != cache_.label_by_name.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<LabelDef>> MetadataServiceImpl::getLabelDefById(LabelId id) {
    auto it = cache_.label_by_id.find(id);
    if (it != cache_.label_by_id.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::vector<LabelDef>> MetadataServiceImpl::listLabels() {
    std::vector<LabelDef> result;
    result.reserve(cache_.label_by_id.size());
    for (auto& [_, def] : cache_.label_by_id) {
        result.push_back(def);
    }
    co_return result;
}

// ==================== EdgeLabel management ====================

folly::coro::Task<EdgeLabelId> MetadataServiceImpl::createEdgeLabel(const std::string& name,
                                                                    const std::vector<PropertyDef>& properties) {
    if (cache_.edge_label_by_name.count(name)) {
        spdlog::error("EdgeLabel '{}' already exists", name);
        co_return INVALID_EDGE_LABEL_ID;
    }

    EdgeLabelId id = cache_.next_edge_label_id++;

    EdgeLabelDef def;
    def.id = id;
    def.name = name;
    uint16_t prop_id = 1;
    for (const auto& p : properties) {
        PropertyDef pd = p;
        pd.id = prop_id++;
        def.properties.push_back(std::move(pd));
    }

    auto encoded = MetadataCodec::encodeEdgeLabelDef(def);
    std::string name_key = std::string("M|edge_label:") + name;
    std::string id_key = std::string("M|edge_label_id:") + std::to_string(id);

    store_->metadataPut(name_key, encoded);
    store_->metadataPut(id_key, encoded);
    saveNextIds();

    if (!store_->createEdgeLabel(id)) {
        spdlog::error("Failed to create storage tables for edge_label '{}' (id={})", name, id);
        co_return INVALID_EDGE_LABEL_ID;
    }

    cache_.edge_label_by_name[name] = def;
    cache_.edge_label_by_id[id] = def;

    spdlog::info("Created edge_label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

folly::coro::Task<std::optional<EdgeLabelId>> MetadataServiceImpl::getEdgeLabelId(const std::string& name) {
    auto it = cache_.edge_label_by_name.find(name);
    if (it != cache_.edge_label_by_name.end())
        co_return it->second.id;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<std::string>> MetadataServiceImpl::getEdgeLabelName(EdgeLabelId id) {
    auto it = cache_.edge_label_by_id.find(id);
    if (it != cache_.edge_label_by_id.end())
        co_return it->second.name;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<EdgeLabelDef>> MetadataServiceImpl::getEdgeLabelDef(const std::string& name) {
    auto it = cache_.edge_label_by_name.find(name);
    if (it != cache_.edge_label_by_name.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<EdgeLabelDef>> MetadataServiceImpl::getEdgeLabelDefById(EdgeLabelId id) {
    auto it = cache_.edge_label_by_id.find(id);
    if (it != cache_.edge_label_by_id.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::vector<EdgeLabelDef>> MetadataServiceImpl::listEdgeLabels() {
    std::vector<EdgeLabelDef> result;
    result.reserve(cache_.edge_label_by_id.size());
    for (auto& [_, def] : cache_.edge_label_by_id) {
        result.push_back(def);
    }
    co_return result;
}

// ==================== ID allocation ====================

folly::coro::Task<VertexId> MetadataServiceImpl::nextVertexId() {
    VertexId id = cache_.next_vertex_id++;
    saveNextIds();
    co_return id;
}

folly::coro::Task<EdgeId> MetadataServiceImpl::nextEdgeId() {
    EdgeId id = cache_.next_edge_id++;
    saveNextIds();
    co_return id;
}

// ==================== Private ====================

void MetadataServiceImpl::saveNextIds() {
    auto encoded = MetadataCodec::encodeNextIds(cache_.next_vertex_id, cache_.next_edge_id, cache_.next_label_id,
                                                cache_.next_edge_label_id);
    store_->metadataPut("M|next_ids", encoded);
}

} // namespace eugraph
