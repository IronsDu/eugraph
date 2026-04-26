#include "storage/meta/async_graph_meta_store.hpp"
#include "common/types/constants.hpp"
#include "storage/meta/i_sync_graph_meta_store.hpp"
#include "storage/meta/meta_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Lifecycle ====================

folly::coro::Task<bool> AsyncGraphMetaStore::open(ISyncGraphMetaStore& store, IoScheduler& io) {
    store_ = &store;
    io_ = &io;

    // Scan M|label:* entries
    co_await io_->dispatchVoid([&]() {
        store_->metadataScan("M|label:", [&](std::string_view key, std::string_view value) {
            std::string name(key.substr(8)); // skip "M|label:"
            auto def = MetadataCodec::decodeLabelDef(value);
            schema_.labels[def.id] = def;
            schema_.label_name_to_id[name] = def.id;
            if (def.id >= schema_.next_label_id)
                schema_.next_label_id = static_cast<LabelId>(def.id + 1);
            return true;
        });
    });

    // Scan M|edge_label:* entries
    co_await io_->dispatchVoid([&]() {
        store_->metadataScan("M|edge_label:", [&](std::string_view key, std::string_view value) {
            std::string name(key.substr(13)); // skip "M|edge_label:"
            auto def = MetadataCodec::decodeEdgeLabelDef(value);
            schema_.edge_labels[def.id] = def;
            schema_.edge_label_name_to_id[name] = def.id;
            if (def.id >= schema_.next_edge_label_id)
                schema_.next_edge_label_id = static_cast<EdgeLabelId>(def.id + 1);
            return true;
        });
    });

    // Load ID counters
    auto ids_data = co_await io_->dispatch([&]() { return store_->metadataGet("M|next_ids"); });
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
    io_ = nullptr;
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

    co_await io_->dispatchVoid([&]() {
        store_->metadataPut(name_key, encoded);
        store_->metadataPut(id_key, encoded);
    });
    co_await saveNextIds();

    // NOTE: No longer calls store_->createLabel(id) — data table creation is the handler's responsibility.

    schema_.labels[id] = def;
    schema_.label_name_to_id[name] = id;

    spdlog::info("Created label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

// ==================== Index management ====================

namespace {

void persistLabelDef(ISyncGraphMetaStore* store, const LabelDef& def) {
    auto encoded = MetadataCodec::encodeLabelDef(def);
    store->metadataPut(std::string("M|label:") + def.name, encoded);
    store->metadataPut(std::string("M|label_id:") + std::to_string(def.id), encoded);
}

void persistEdgeLabelDef(ISyncGraphMetaStore* store, const EdgeLabelDef& def) {
    auto encoded = MetadataCodec::encodeEdgeLabelDef(def);
    store->metadataPut(std::string("M|edge_label:") + def.name, encoded);
    store->metadataPut(std::string("M|edge_label_id:") + std::to_string(def.id), encoded);
}

std::optional<uint16_t> findPropId(const std::vector<PropertyDef>& props, const std::string& name) {
    for (const auto& p : props)
        if (p.name == name)
            return p.id;
    return std::nullopt;
}

} // namespace

folly::coro::Task<bool> AsyncGraphMetaStore::createVertexIndex(const std::string& name, const std::string& label_name,
                                                                const std::string& prop_name, bool unique) {
    if (schema_.findIndexByName(name).has_value()) {
        spdlog::error("Index '{}' already exists", name);
        co_return false;
    }

    auto label_opt = schema_.getLabel(label_name);
    if (!label_opt) {
        spdlog::error("Label '{}' not found", label_name);
        co_return false;
    }

    auto prop_id = findPropId(label_opt->properties, prop_name);
    if (!prop_id) {
        spdlog::error("Property '{}' not found in label '{}'", prop_name, label_name);
        co_return false;
    }

    for (const auto& idx : label_opt->indexes) {
        if (idx.prop_ids.size() == 1 && idx.prop_ids[0] == *prop_id) {
            spdlog::error("Index already exists on {}.{}", label_name, prop_name);
            co_return false;
        }
    }

    LabelDef::IndexDef idx_def;
    idx_def.name = name;
    idx_def.prop_ids = {*prop_id};
    idx_def.unique = unique;
    idx_def.state = IndexState::WRITE_ONLY;

    schema_.labels[label_opt->id].indexes.push_back(idx_def);

    co_await io_->dispatchVoid([&]() {
        persistLabelDef(store_, schema_.labels[label_opt->id]);
        std::string idx_key = std::string("M|index:") + name;
        std::string idx_val;
        idx_val.push_back(static_cast<char>(label_opt->id >> 8));
        idx_val.push_back(static_cast<char>(label_opt->id & 0xFF));
        idx_val.push_back(static_cast<char>(*prop_id >> 8));
        idx_val.push_back(static_cast<char>(*prop_id & 0xFF));
        idx_val.push_back(static_cast<char>(0)); // is_edge = false
        store_->metadataPut(idx_key, idx_val);
    });

    spdlog::info("Created vertex index '{}' on {}.{}", name, label_name, prop_name);
    co_return true;
}

folly::coro::Task<bool> AsyncGraphMetaStore::createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                                              const std::string& prop_name, bool unique) {
    if (schema_.findIndexByName(name).has_value()) {
        spdlog::error("Index '{}' already exists", name);
        co_return false;
    }

    auto elabel_opt = schema_.getEdgeLabel(edge_label_name);
    if (!elabel_opt) {
        spdlog::error("EdgeLabel '{}' not found", edge_label_name);
        co_return false;
    }

    auto prop_id = findPropId(elabel_opt->properties, prop_name);
    if (!prop_id) {
        spdlog::error("Property '{}' not found in edge_label '{}'", prop_name, edge_label_name);
        co_return false;
    }

    for (const auto& idx : elabel_opt->indexes) {
        if (idx.prop_ids.size() == 1 && idx.prop_ids[0] == *prop_id) {
            spdlog::error("Index already exists on {}.{}", edge_label_name, prop_name);
            co_return false;
        }
    }

    LabelDef::IndexDef idx_def;
    idx_def.name = name;
    idx_def.prop_ids = {*prop_id};
    idx_def.unique = unique;
    idx_def.state = IndexState::WRITE_ONLY;

    schema_.edge_labels[elabel_opt->id].indexes.push_back(idx_def);

    co_await io_->dispatchVoid([&]() {
        persistEdgeLabelDef(store_, schema_.edge_labels[elabel_opt->id]);
        std::string idx_key = std::string("M|index:") + name;
        std::string idx_val;
        idx_val.push_back(static_cast<char>(elabel_opt->id >> 8));
        idx_val.push_back(static_cast<char>(elabel_opt->id & 0xFF));
        idx_val.push_back(static_cast<char>(*prop_id >> 8));
        idx_val.push_back(static_cast<char>(*prop_id & 0xFF));
        idx_val.push_back(static_cast<char>(1)); // is_edge = true
        store_->metadataPut(idx_key, idx_val);
    });

    spdlog::info("Created edge index '{}' on {}.{}", name, edge_label_name, prop_name);
    co_return true;
}

folly::coro::Task<bool> AsyncGraphMetaStore::updateIndexState(const std::string& name, IndexState new_state) {
    for (auto& [lid, label] : schema_.labels) {
        for (auto& idx : label.indexes) {
            if (idx.name == name) {
                idx.state = new_state;
                co_await io_->dispatchVoid([&]() { persistLabelDef(store_, label); });
                co_return true;
            }
        }
    }
    for (auto& [elid, elabel] : schema_.edge_labels) {
        for (auto& idx : elabel.indexes) {
            if (idx.name == name) {
                idx.state = new_state;
                co_await io_->dispatchVoid([&]() { persistEdgeLabelDef(store_, elabel); });
                co_return true;
            }
        }
    }
    spdlog::error("Index '{}' not found for state update", name);
    co_return false;
}

folly::coro::Task<bool> AsyncGraphMetaStore::dropIndex(const std::string& name) {
    for (auto& [lid, label] : schema_.labels) {
        for (auto it = label.indexes.begin(); it != label.indexes.end(); ++it) {
            if (it->name == name) {
                label.indexes.erase(it);
                co_await io_->dispatchVoid([&]() {
                    persistLabelDef(store_, label);
                    store_->metadataDel(std::string("M|index:") + name);
                });
                co_return true;
            }
        }
    }
    for (auto& [elid, elabel] : schema_.edge_labels) {
        for (auto it = elabel.indexes.begin(); it != elabel.indexes.end(); ++it) {
            if (it->name == name) {
                elabel.indexes.erase(it);
                co_await io_->dispatchVoid([&]() {
                    persistEdgeLabelDef(store_, elabel);
                    store_->metadataDel(std::string("M|index:") + name);
                });
                co_return true;
            }
        }
    }
    spdlog::error("Index '{}' not found for drop", name);
    co_return false;
}

folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexInfo>> AsyncGraphMetaStore::listIndexes() {
    std::vector<IndexInfo> result;
    for (auto& [lid, label] : schema_.labels) {
        for (auto& idx : label.indexes) {
            IndexInfo info;
            info.name = idx.name;
            info.label_name = label.name;
            info.unique = idx.unique;
            info.is_edge = false;
            info.state = idx.state;
            if (idx.prop_ids.size() == 1) {
                for (auto& p : label.properties) {
                    if (p.id == idx.prop_ids[0]) {
                        info.property_name = p.name;
                        break;
                    }
                }
            }
            result.push_back(std::move(info));
        }
    }
    for (auto& [elid, elabel] : schema_.edge_labels) {
        for (auto& idx : elabel.indexes) {
            IndexInfo info;
            info.name = idx.name;
            info.label_name = elabel.name;
            info.unique = idx.unique;
            info.is_edge = true;
            info.state = idx.state;
            if (idx.prop_ids.size() == 1) {
                for (auto& p : elabel.properties) {
                    if (p.id == idx.prop_ids[0]) {
                        info.property_name = p.name;
                        break;
                    }
                }
            }
            result.push_back(std::move(info));
        }
    }
    co_return result;
}

folly::coro::Task<std::optional<IAsyncGraphMetaStore::IndexInfo>> AsyncGraphMetaStore::getIndex(const std::string& name) {
    auto all = co_await listIndexes();
    for (auto& info : all) {
        if (info.name == name)
            co_return info;
    }
    co_return std::nullopt;
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

    spdlog::info("{} 11111111", __FUNCTION__);
    co_await io_->dispatchVoid([&]() {
        spdlog::info("{} meta put", __FUNCTION__);
        store_->metadataPut(name_key, encoded);
        store_->metadataPut(id_key, encoded);
    });

    spdlog::info("{} 2222222222222", __FUNCTION__);
    co_await saveNextIds();

    spdlog::info("{} 33333333333", __FUNCTION__);
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
    co_await saveNextIds();
    co_return id;
}

folly::coro::Task<EdgeId> AsyncGraphMetaStore::nextEdgeId() {
    EdgeId id = schema_.next_edge_id++;
    co_await saveNextIds();
    co_return id;
}

// ==================== Private ====================

folly::coro::Task<void> AsyncGraphMetaStore::saveNextIds() {
    auto encoded = MetadataCodec::encodeNextIds(schema_.next_vertex_id, schema_.next_edge_id, schema_.next_label_id,
                                                schema_.next_edge_label_id);
    co_await io_->dispatchVoid([this, encoded = std::move(encoded)]() { store_->metadataPut("M|next_ids", encoded); });
}

} // namespace eugraph
