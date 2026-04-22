#include "storage/meta/async_graph_meta_store.hpp"
#include "common/types/constants.hpp"
#include "storage/meta/i_sync_graph_meta_store.hpp"
#include "storage/meta/meta_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Lifecycle ====================

folly::coro::Task<bool> AsyncGraphMetaStore::open(ISyncGraphMetaStore& store) {
    store_ = &store;

    // Scan M|label:* entries
    store_->metadataScan("M|label:", [&](std::string_view key, std::string_view value) {
        std::string name(key.substr(8)); // skip "M|label:"
        auto def = MetadataCodec::decodeLabelDef(value);
        schema_.labels[def.id] = def;
        schema_.label_name_to_id[name] = def.id;
        if (def.id >= schema_.next_label_id)
            schema_.next_label_id = static_cast<LabelId>(def.id + 1);
        return true;
    });

    // Scan M|edge_label:* entries
    store_->metadataScan("M|edge_label:", [&](std::string_view key, std::string_view value) {
        std::string name(key.substr(13)); // skip "M|edge_label:"
        auto def = MetadataCodec::decodeEdgeLabelDef(value);
        schema_.edge_labels[def.id] = def;
        schema_.edge_label_name_to_id[name] = def.id;
        if (def.id >= schema_.next_edge_label_id)
            schema_.next_edge_label_id = static_cast<EdgeLabelId>(def.id + 1);
        return true;
    });

    // Load ID counters
    auto ids_data = store_->metadataGet("M|next_ids");
    if (ids_data) {
        MetadataCodec::decodeNextIds(*ids_data, schema_.next_vertex_id, schema_.next_edge_id, schema_.next_label_id,
                                     schema_.next_edge_label_id);
    }

    spdlog::info("AsyncGraphMetaStore opened: {} labels, {} edge labels, next_vid={}, next_eid={}",
                 schema_.labels.size(), schema_.edge_labels.size(), schema_.next_vertex_id, schema_.next_edge_id);

    co_return true;
}

folly::coro::Task<void> AsyncGraphMetaStore::close() {
    store_ = nullptr;
    schema_ = GraphSchema{};
    co_return;
}

// ==================== Label management ====================

folly::coro::Task<LabelId> AsyncGraphMetaStore::createLabel(const std::string& name,
                                                            const std::vector<PropertyDef>& properties) {
    if (schema_.label_name_to_id.count(name)) {
        spdlog::error("Label '{}' already exists", name);
        co_return INVALID_LABEL_ID;
    }

    LabelId id = schema_.next_label_id++;

    LabelDef def;
    def.id = id;
    def.name = name;
    uint16_t prop_id = 1;
    for (const auto& p : properties) {
        PropertyDef pd = p;
        pd.id = prop_id++;
        def.properties.push_back(std::move(pd));
    }

    auto encoded = MetadataCodec::encodeLabelDef(def);
    std::string name_key = std::string("M|label:") + name;
    std::string id_key = std::string("M|label_id:") + std::to_string(id);

    store_->metadataPut(name_key, encoded);
    store_->metadataPut(id_key, encoded);
    saveNextIds();

    // NOTE: No longer calls store_->createLabel(id) — data table creation is the handler's responsibility.

    schema_.labels[id] = def;
    schema_.label_name_to_id[name] = id;

    spdlog::info("Created label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

folly::coro::Task<std::optional<LabelId>> AsyncGraphMetaStore::getLabelId(const std::string& name) {
    auto it = schema_.label_name_to_id.find(name);
    if (it != schema_.label_name_to_id.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<std::string>> AsyncGraphMetaStore::getLabelName(LabelId id) {
    auto it = schema_.labels.find(id);
    if (it != schema_.labels.end())
        co_return it->second.name;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<LabelDef>> AsyncGraphMetaStore::getLabelDef(const std::string& name) {
    co_return schema_.getLabel(name);
}

folly::coro::Task<std::optional<LabelDef>> AsyncGraphMetaStore::getLabelDefById(LabelId id) {
    co_return schema_.getLabel(id);
}

folly::coro::Task<std::vector<LabelDef>> AsyncGraphMetaStore::listLabels() {
    std::vector<LabelDef> result;
    result.reserve(schema_.labels.size());
    for (auto& [_, def] : schema_.labels) {
        result.push_back(def);
    }
    co_return result;
}

// ==================== EdgeLabel management ====================

folly::coro::Task<EdgeLabelId> AsyncGraphMetaStore::createEdgeLabel(const std::string& name,
                                                                    const std::vector<PropertyDef>& properties) {
    if (schema_.edge_label_name_to_id.count(name)) {
        spdlog::error("EdgeLabel '{}' already exists", name);
        co_return INVALID_EDGE_LABEL_ID;
    }

    EdgeLabelId id = schema_.next_edge_label_id++;

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

    // NOTE: No longer calls store_->createEdgeLabel(id).

    schema_.edge_labels[id] = def;
    schema_.edge_label_name_to_id[name] = id;

    spdlog::info("Created edge_label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

folly::coro::Task<std::optional<EdgeLabelId>> AsyncGraphMetaStore::getEdgeLabelId(const std::string& name) {
    auto it = schema_.edge_label_name_to_id.find(name);
    if (it != schema_.edge_label_name_to_id.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<std::string>> AsyncGraphMetaStore::getEdgeLabelName(EdgeLabelId id) {
    auto it = schema_.edge_labels.find(id);
    if (it != schema_.edge_labels.end())
        co_return it->second.name;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<EdgeLabelDef>> AsyncGraphMetaStore::getEdgeLabelDef(const std::string& name) {
    co_return schema_.getEdgeLabel(name);
}

folly::coro::Task<std::optional<EdgeLabelDef>> AsyncGraphMetaStore::getEdgeLabelDefById(EdgeLabelId id) {
    co_return schema_.getEdgeLabel(id);
}

folly::coro::Task<std::vector<EdgeLabelDef>> AsyncGraphMetaStore::listEdgeLabels() {
    std::vector<EdgeLabelDef> result;
    result.reserve(schema_.edge_labels.size());
    for (auto& [_, def] : schema_.edge_labels) {
        result.push_back(def);
    }
    co_return result;
}

// ==================== ID allocation ====================

folly::coro::Task<VertexId> AsyncGraphMetaStore::nextVertexId() {
    VertexId id = schema_.next_vertex_id++;
    saveNextIds();
    co_return id;
}

folly::coro::Task<EdgeId> AsyncGraphMetaStore::nextEdgeId() {
    EdgeId id = schema_.next_edge_id++;
    saveNextIds();
    co_return id;
}

// ==================== Private ====================

void AsyncGraphMetaStore::saveNextIds() {
    auto encoded = MetadataCodec::encodeNextIds(schema_.next_vertex_id, schema_.next_edge_id, schema_.next_label_id,
                                                schema_.next_edge_label_id);
    store_->metadataPut("M|next_ids", encoded);
}

} // namespace eugraph
