#include "storage/meta/async_graph_meta_store.hpp"
#include "common/types/constants.hpp"
#include "storage/meta/i_sync_graph_meta_store.hpp"
#include "storage/meta/meta_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Lifecycle ====================

void AsyncGraphMetaStore::publishSchemaVersion() {
    auto next = std::make_shared<GraphSchema>(schema_);
    std::lock_guard<std::mutex> lock(published_mu_);
    published_schema_ = std::move(next);
    // 代数与 schema 在同一把锁内发布：读者不会看到"新代数 + 旧 schema"。
    published_generation_.store(schema_generation_.load(std::memory_order_acquire), std::memory_order_release);
}

folly::coro::Task<bool> AsyncGraphMetaStore::open(ISyncGraphMetaStore& store, IoScheduler& io) {
    store_ = std::ref(store);
    io_ = std::ref(io);
    auto& s = store_->get();
    auto& i = io_->get();

    // Scan M|label:* entries
    co_await i.dispatchVoid([&]() {
        s.metadataScan("M|label:", [&](std::string_view key, std::string_view value) {
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
    co_await i.dispatchVoid([&]() {
        s.metadataScan("M|edge_label:", [&](std::string_view key, std::string_view value) {
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
    auto ids_data = co_await i.dispatch([&]() { return s.metadataGet("M|next_ids"); });
    if (ids_data) {
        MetadataCodec::decodeNextIds(*ids_data, schema_.next_vertex_id, schema_.next_edge_id, schema_.next_label_id,
                                     schema_.next_edge_label_id, schema_.next_index_id);
    }

    spdlog::info("AsyncGraphMetaStore opened: {} labels, {} edge labels, next_vid={}, next_eid={}",
                 schema_.labels.size(), schema_.edge_labels.size(), schema_.next_vertex_id, schema_.next_edge_id);

    publishSchemaVersion();
    co_return true;
}

folly::coro::Task<void> AsyncGraphMetaStore::close() {
    store_ = std::nullopt;
    io_ = std::nullopt;
    schema_ = GraphSchema{};
    {
        std::lock_guard<std::mutex> lock(published_mu_);
        published_schema_.reset();
    }
    co_return;
}

// ==================== Label management ====================

namespace {

void persistLabelDef(ISyncGraphMetaStore& store, const LabelDef& def) {
    auto encoded = MetadataCodec::encodeLabelDef(def);
    const bool ok = store.metadataPut(std::string("M|label:") + def.name, encoded) &&
                    store.metadataPut(std::string("M|label_id:") + std::to_string(def.id), encoded);
    if (!ok)
        throw std::runtime_error("Failed to persist label definition '" + def.name + "'");
}

} // namespace

folly::coro::Task<LabelId> AsyncGraphMetaStore::createLabel(const std::string& name,
                                                            const std::vector<PropertyDef>& properties,
                                                            const std::vector<std::string>& pk_props,
                                                            bool merge_properties) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    // 已存在时的两种语义（loader 每次运行都会先 createLabel，这是它的正常路径）：
    //   merge_properties=true  → 增量加属性：同名必须同类型（否则报错），新属性按声明顺序追加 prop_id。
    //                            对应 neo4j-admin import：同一标签可由多个文件供给，属性取并集。
    //   merge_properties=false → 要求「属性 + 主键」声明完全一致（幂等重跑），否则报错。
    if (auto existing_it = schema_.label_name_to_id.find(name); existing_it != schema_.label_name_to_id.end()) {
        const LabelId existing_id = existing_it->second;
        const LabelDef before = schema_.labels.at(existing_id);

        if (merge_properties) {
            LabelDef def = schema_.labels.at(existing_id);
            uint16_t next_prop_id = static_cast<uint16_t>(def.properties.size());
            bool changed = false;
            for (const auto& p : properties) {
                const PropertyDef* found = nullptr;
                for (const auto& pd : def.properties) {
                    if (pd.name == p.name) {
                        found = &pd;
                        break;
                    }
                }
                if (found != nullptr) {
                    if (found->type != p.type) {
                        throw std::runtime_error("Label '" + name + "' property '" + p.name +
                                                 "' already exists with a different type");
                    }
                    continue;
                }
                PropertyDef pd = p;
                pd.id = next_prop_id++;
                def.properties.push_back(std::move(pd));
                changed = true;
            }
            if (changed) {
                auto& s = store_->get();
                auto& i = io_->get();
                const LabelDef snapshot = def;
                co_await i.dispatchVoid([&s, &snapshot]() { persistLabelDef(s, snapshot); });
                {
                    std::unique_lock<std::shared_mutex> lock(schema_mu_);
                    schema_.labels[existing_id] = std::move(def);
                }
                bumpSchemaGeneration();
                publishSchemaVersion();
                spdlog::info("Merged properties into label '{}' ({} -> {} properties)", name, before.properties.size(),
                             snapshot.properties.size());
            }
            co_return existing_id;
        }

        if (before.properties.size() != properties.size()) {
            throw std::runtime_error("Label '" + name + "' already exists with " +
                                     std::to_string(before.properties.size()) + " properties (requested " +
                                     std::to_string(properties.size()) +
                                     "); pass merge_properties to add properties incrementally");
        }
        for (size_t i = 0; i < properties.size(); ++i) {
            if (before.properties[i].name != properties[i].name || before.properties[i].type != properties[i].type) {
                throw std::runtime_error("Label '" + name + "' already exists; property #" + std::to_string(i) +
                                         " differs (existing '" + before.properties[i].name + "', requested '" +
                                         properties[i].name + "')");
            }
        }
        std::vector<std::string> existing_pk;
        existing_pk.reserve(before.pk_prop_ids.size());
        for (auto pid : before.pk_prop_ids) {
            if (pid < before.properties.size())
                existing_pk.push_back(before.properties[pid].name);
        }
        if (existing_pk != pk_props) {
            throw std::runtime_error("Label '" + name + "' already exists with a different primary key declaration");
        }
        co_return existing_id;
    }

    LabelId id;
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        id = schema_.next_label_id++;
    }
    auto& s = store_->get();
    auto& i = io_->get();

    LabelDef def;
    def.id = id;
    def.name = name;
    uint16_t prop_id = 0;
    for (const auto& p : properties) {
        PropertyDef pd = p;
        pd.id = prop_id++;
        def.properties.push_back(std::move(pd));
    }

    // 主键属性名 → prop_id（**有序**，顺序即主键元组顺序）；名字必须已在 properties 中声明
    for (const auto& pk_name : pk_props) {
        uint16_t pid = UINT16_MAX;
        for (const auto& pd : def.properties) {
            if (pd.name == pk_name) {
                pid = pd.id;
                break;
            }
        }
        if (pid == UINT16_MAX)
            throw std::runtime_error("Primary key property '" + pk_name + "' is not declared on label '" + name + "'");
        if (std::find(def.pk_prop_ids.begin(), def.pk_prop_ids.end(), pid) != def.pk_prop_ids.end())
            throw std::runtime_error("Duplicate primary key property '" + pk_name + "' on label '" + name + "'");
        def.pk_prop_ids.push_back(pid);
    }

    auto encoded = MetadataCodec::encodeLabelDef(def);
    std::string name_key = std::string("M|label:") + name;
    std::string id_key = std::string("M|label_id:") + std::to_string(id);

    const bool persisted =
        co_await i.dispatch([&]() { return s.metadataPut(name_key, encoded) && s.metadataPut(id_key, encoded); });
    if (!persisted)
        throw std::runtime_error("Failed to persist label definition '" + name + "'");
    co_await saveNextIds();

    // NOTE: No longer calls store.createLabel(id) — data table creation is the handler's responsibility.

    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.labels[id] = def;
        schema_.label_name_to_id[name] = id;
    }
    bumpSchemaGeneration();
    publishSchemaVersion();

    spdlog::info("Created label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

// ==================== Index management ====================

namespace {

void persistEdgeLabelDef(ISyncGraphMetaStore& store, const EdgeLabelDef& def) {
    auto encoded = MetadataCodec::encodeEdgeLabelDef(def);
    const bool ok = store.metadataPut(std::string("M|edge_label:") + def.name, encoded) &&
                    store.metadataPut(std::string("M|edge_label_id:") + std::to_string(def.id), encoded);
    if (!ok)
        throw std::runtime_error("Failed to persist edge label definition '" + def.name + "'");
}

std::optional<uint16_t> findPropId(const std::vector<PropertyDef>& props, const std::string& name) {
    for (const auto& p : props)
        if (p.name == name)
            return p.id;
    return std::nullopt;
}

} // namespace

folly::coro::Task<bool> AsyncGraphMetaStore::createVertexIndex(const std::string& name, const std::string& label_name,
                                                               const std::vector<std::string>& prop_names,
                                                               bool unique) {
    std::vector<IndexAccessorDef> accessors;
    accessors.reserve(prop_names.size());
    for (const auto& pn : prop_names) {
        IndexAccessorDef acc;
        acc.property_name = pn;
        accessors.push_back(std::move(acc));
    }
    co_return co_await createVertexIndexWithAccessors(name, label_name, accessors, unique);
}

folly::coro::Task<bool>
AsyncGraphMetaStore::createVertexIndexWithAccessors(const std::string& name, const std::string& label_name,
                                                    const std::vector<IndexAccessorDef>& accessors, bool unique) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    if (schema_.findIndexByName(name).has_value()) {
        spdlog::error("Index '{}' already exists", name);
        co_return false;
    }

    auto label_opt = schema_.getLabel(label_name);
    if (!label_opt) {
        spdlog::error("Label '{}' not found", label_name);
        co_return false;
    }
    if (accessors.empty()) {
        spdlog::error("Index '{}' has no property accessors", name);
        co_return false;
    }

    // Resolve strong accessors against the source label.
    std::vector<IndexAccessorDef> resolved = accessors;
    for (auto& acc : resolved) {
        if (!acc.is_strong)
            continue;
        auto source_opt = schema_.getLabel(acc.source_label_id);
        if (!source_opt) {
            spdlog::error("Source label '{}' not found for strong index '{}'", acc.source_label_id, name);
            co_return false;
        }
        auto prop_id = findPropId(source_opt->properties, acc.property_name);
        if (!prop_id) {
            spdlog::error("Property '{}' not found in source label '{}' for strong index '{}'", acc.property_name,
                          source_opt->name, name);
            co_return false;
        }
    }

    LabelDef::IndexDef idx_def;
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        idx_def.index_id = schema_.next_index_id++;
    }
    idx_def.name = name;
    idx_def.accessors = resolved;
    idx_def.unique = unique;
    idx_def.state = IndexState::WRITE_ONLY;

    auto same_accessors = [](const auto& a, const auto& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].is_strong != b[i].is_strong || a[i].source_label_id != b[i].source_label_id ||
                a[i].property_name != b[i].property_name)
                return false;
        }
        return true;
    };

    for (const auto& idx : label_opt->indexes) {
        if (same_accessors(idx.accessors, idx_def.accessors)) {
            spdlog::error("Index already exists on same property set for label '{}'", label_name);
            co_return false;
        }
    }

    LabelDef updated_label = schema_.labels[label_opt->id];
    updated_label.indexes.push_back(idx_def);

    auto& s = store_->get();
    auto& i = io_->get();
    try {
        co_await i.dispatchVoid([&]() {
            persistLabelDef(s, updated_label);
            std::string idx_key = std::string("M|index:") + name;
            std::string idx_val;
            idx_val.push_back(static_cast<char>(label_opt->id >> 8));
            idx_val.push_back(static_cast<char>(label_opt->id & 0xFF));
            idx_val.push_back(static_cast<char>(static_cast<uint16_t>(idx_def.accessors.size()) >> 8));
            idx_val.push_back(static_cast<char>(static_cast<uint16_t>(idx_def.accessors.size()) & 0xFF));
            for (const auto& acc : idx_def.accessors) {
                idx_val.push_back(static_cast<char>(acc.is_strong ? 1 : 0));
                idx_val.push_back(static_cast<char>(acc.source_label_id >> 8));
                idx_val.push_back(static_cast<char>(acc.source_label_id & 0xFF));
                idx_val.push_back(static_cast<char>(acc.property_name.size()));
                for (unsigned char ch : acc.property_name)
                    idx_val.push_back(static_cast<char>(ch));
            }
            idx_val.push_back(static_cast<char>(0)); // is_edge = false
            if (!s.metadataPut(idx_key, idx_val))
                throw std::runtime_error("Failed to persist index definition '" + name + "'");
        });
        co_await saveNextIds();
    } catch (...) {
        throw;
    }
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.labels[label_opt->id] = std::move(updated_label);
    }

    bumpSchemaGeneration();
    publishSchemaVersion();
    std::string prop_list;
    for (size_t i = 0; i < idx_def.accessors.size(); ++i) {
        if (i > 0)
            prop_list += ", ";
        prop_list += idx_def.accessors[i].property_name;
    }
    spdlog::info("Created vertex index '{}' (id={}) on {}.({})", name, idx_def.index_id, label_name, prop_list);
    co_return true;
}

folly::coro::Task<bool> AsyncGraphMetaStore::createEdgeIndex(const std::string& name,
                                                             const std::string& edge_label_name,
                                                             const std::vector<std::string>& prop_names, bool unique) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    if (schema_.findIndexByName(name).has_value()) {
        spdlog::error("Index '{}' already exists", name);
        co_return false;
    }

    auto elabel_opt = schema_.getEdgeLabel(edge_label_name);
    if (!elabel_opt) {
        spdlog::error("EdgeLabel '{}' not found", edge_label_name);
        co_return false;
    }

    std::vector<uint16_t> new_prop_ids;
    for (const auto& pn : prop_names) {
        auto prop_id = findPropId(elabel_opt->properties, pn);
        if (!prop_id) {
            spdlog::error("Property '{}' not found in edge_label '{}'", pn, edge_label_name);
            co_return false;
        }
        new_prop_ids.push_back(*prop_id);
    }

    for (const auto& idx : elabel_opt->indexes) {
        if (idx.prop_ids == new_prop_ids) {
            spdlog::error("Index already exists on same property set for edge_label '{}'", edge_label_name);
            co_return false;
        }
    }

    LabelDef::IndexDef idx_def;
    // v2: 边索引也必须分配 index_id（此前漏了 ⇒ 恒为 0，任何按 id 的路由/注册都不成立）。
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        idx_def.index_id = schema_.next_index_id++;
    }
    idx_def.name = name;
    idx_def.prop_ids = new_prop_ids;
    idx_def.unique = unique;
    idx_def.state = IndexState::WRITE_ONLY;

    EdgeLabelDef updated_elabel = schema_.edge_labels[elabel_opt->id];
    updated_elabel.indexes.push_back(idx_def);

    auto& s = store_->get();
    auto& i = io_->get();
    try {
        co_await i.dispatchVoid([&]() {
            persistEdgeLabelDef(s, updated_elabel);
            std::string idx_key = std::string("M|index:") + name;
            std::string idx_val;
            idx_val.push_back(static_cast<char>(elabel_opt->id >> 8));
            idx_val.push_back(static_cast<char>(elabel_opt->id & 0xFF));
            idx_val.push_back(static_cast<char>(static_cast<uint16_t>(new_prop_ids.size()) >> 8));
            idx_val.push_back(static_cast<char>(static_cast<uint16_t>(new_prop_ids.size()) & 0xFF));
            for (auto pid : new_prop_ids) {
                idx_val.push_back(static_cast<char>(pid >> 8));
                idx_val.push_back(static_cast<char>(pid & 0xFF));
            }
            idx_val.push_back(static_cast<char>(1)); // is_edge = true
            if (!s.metadataPut(idx_key, idx_val))
                throw std::runtime_error("Failed to persist edge index definition '" + name + "'");
        });
        // index_id 是持久化字段（meta_codec::encodeIndexDef），必须与定义一起落盘计数器。
        co_await saveNextIds();
    } catch (...) {
        throw;
    }
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.edge_labels[elabel_opt->id] = std::move(updated_elabel);
    }

    bumpSchemaGeneration();
    publishSchemaVersion();
    std::string edge_prop_list;
    for (size_t i = 0; i < prop_names.size(); ++i) {
        if (i > 0)
            edge_prop_list += ", ";
        edge_prop_list += prop_names[i];
    }
    spdlog::info("Created edge index '{}' on {}.({})", name, edge_label_name, edge_prop_list);
    co_return true;
}

folly::coro::Task<bool> AsyncGraphMetaStore::updateIndexState(const std::string& name, IndexState new_state) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    const GraphSchema snapshot = schemaSnapshot();
    auto& s = store_->get();
    auto& i = io_->get();

    for (const auto& [lid, label] : snapshot.labels) {
        for (const auto& idx : label.indexes) {
            if (idx.name != name)
                continue;
            const IndexState before = idx.state;
            LabelDef updated = label;
            for (auto& candidate : updated.indexes) {
                if (candidate.name == name) {
                    candidate.state = new_state;
                    break;
                }
            }
            co_await i.dispatchVoid([&]() { persistLabelDef(s, updated); });
            {
                std::unique_lock<std::shared_mutex> lock(schema_mu_);
                schema_.labels[lid] = std::move(updated);
            }
            if (new_state == IndexState::PUBLIC && before != IndexState::PUBLIC)
                bumpSchemaGeneration();
            publishSchemaVersion();
            co_return true;
        }
    }
    for (const auto& [elid, elabel] : snapshot.edge_labels) {
        for (const auto& idx : elabel.indexes) {
            if (idx.name != name)
                continue;
            const IndexState before = idx.state;
            EdgeLabelDef updated = elabel;
            for (auto& candidate : updated.indexes) {
                if (candidate.name == name) {
                    candidate.state = new_state;
                    break;
                }
            }
            co_await i.dispatchVoid([&]() { persistEdgeLabelDef(s, updated); });
            {
                std::unique_lock<std::shared_mutex> lock(schema_mu_);
                schema_.edge_labels[elid] = std::move(updated);
            }
            if (new_state == IndexState::PUBLIC && before != IndexState::PUBLIC)
                bumpSchemaGeneration();
            publishSchemaVersion();
            co_return true;
        }
    }
    spdlog::error("Index '{}' not found for state update", name);
    co_return false;
}

folly::coro::Task<bool> AsyncGraphMetaStore::dropIndex(const std::string& name) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    const GraphSchema snapshot = schemaSnapshot();
    auto& s = store_->get();
    auto& i = io_->get();

    for (const auto& [lid, label] : snapshot.labels) {
        for (auto it = label.indexes.begin(); it != label.indexes.end(); ++it) {
            if (it->name != name)
                continue;
            LabelDef updated = label;
            updated.indexes.erase(updated.indexes.begin() + std::distance(label.indexes.begin(), it));
            co_await i.dispatchVoid([&]() {
                persistLabelDef(s, updated);
                if (!s.metadataDel(std::string("M|index:") + name))
                    throw std::runtime_error("Failed to delete index metadata '" + name + "'");
            });
            {
                std::unique_lock<std::shared_mutex> lock(schema_mu_);
                schema_.labels[lid] = std::move(updated);
            }
            bumpSchemaGeneration();
            publishSchemaVersion();
            co_return true;
        }
    }
    for (const auto& [elid, elabel] : snapshot.edge_labels) {
        for (auto it = elabel.indexes.begin(); it != elabel.indexes.end(); ++it) {
            if (it->name != name)
                continue;
            EdgeLabelDef updated = elabel;
            updated.indexes.erase(updated.indexes.begin() + std::distance(elabel.indexes.begin(), it));
            co_await i.dispatchVoid([&]() {
                persistEdgeLabelDef(s, updated);
                if (!s.metadataDel(std::string("M|index:") + name))
                    throw std::runtime_error("Failed to delete index metadata '" + name + "'");
            });
            {
                std::unique_lock<std::shared_mutex> lock(schema_mu_);
                schema_.edge_labels[elid] = std::move(updated);
            }
            bumpSchemaGeneration();
            publishSchemaVersion();
            co_return true;
        }
    }
    spdlog::error("Index '{}' not found for drop", name);
    co_return false;
}

folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexInfo>> AsyncGraphMetaStore::listIndexes() {
    std::vector<IndexInfo> result;
    const GraphSchema snapshot = schemaSnapshot();
    for (const auto& [lid, label] : snapshot.labels) {
        for (const auto& idx : label.indexes) {
            IndexInfo info;
            info.index_id = idx.index_id;
            info.name = idx.name;
            info.label_name = label.name;
            info.unique = idx.unique;
            info.is_edge = false;
            info.state = idx.state;
            if (!idx.accessors.empty()) {
                for (const auto& acc : idx.accessors)
                    info.property_names.push_back(acc.property_name);
            } else {
                for (auto pid : idx.prop_ids) {
                    for (const auto& p : label.properties) {
                        if (p.id == pid) {
                            info.property_names.push_back(p.name);
                            break;
                        }
                    }
                }
            }
            result.push_back(std::move(info));
        }
    }
    for (const auto& [elid, elabel] : snapshot.edge_labels) {
        for (const auto& idx : elabel.indexes) {
            IndexInfo info;
            info.index_id = idx.index_id;
            info.name = idx.name;
            info.label_name = elabel.name;
            info.unique = idx.unique;
            info.is_edge = true;
            info.state = idx.state;
            for (auto pid : idx.prop_ids) {
                for (const auto& p : elabel.properties) {
                    if (p.id == pid) {
                        info.property_names.push_back(p.name);
                        break;
                    }
                }
            }
            result.push_back(std::move(info));
        }
    }
    co_return result;
}

folly::coro::Task<std::optional<IAsyncGraphMetaStore::IndexInfo>>
AsyncGraphMetaStore::getIndex(const std::string& name) {
    auto all = co_await listIndexes();
    for (auto& info : all) {
        if (info.name == name)
            co_return info;
    }
    co_return std::nullopt;
}

folly::coro::Task<std::optional<LabelId>> AsyncGraphMetaStore::getLabelId(const std::string& name) {
    const GraphSchema snapshot = schemaSnapshot();
    auto it = snapshot.label_name_to_id.find(name);
    if (it != snapshot.label_name_to_id.end())
        co_return it->second;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<std::string>> AsyncGraphMetaStore::getLabelName(LabelId id) {
    const GraphSchema snapshot = schemaSnapshot();
    auto it = snapshot.labels.find(id);
    if (it != snapshot.labels.end())
        co_return it->second.name;
    co_return std::nullopt;
}

folly::coro::Task<std::optional<LabelDef>> AsyncGraphMetaStore::getLabelDef(const std::string& name) {
    const GraphSchema snapshot = schemaSnapshot();
    co_return snapshot.getLabel(name);
}

folly::coro::Task<std::optional<LabelDef>> AsyncGraphMetaStore::getLabelDefById(LabelId id) {
    const GraphSchema snapshot = schemaSnapshot();
    co_return snapshot.getLabel(id);
}

folly::coro::Task<std::vector<LabelDef>> AsyncGraphMetaStore::listLabels() {
    const GraphSchema snapshot = schemaSnapshot();
    std::vector<LabelDef> result;
    result.reserve(snapshot.labels.size());
    for (const auto& [_, def] : snapshot.labels) {
        result.push_back(def);
    }
    co_return result;
}

// ==================== EdgeLabel management ====================

folly::coro::Task<EdgeLabelId> AsyncGraphMetaStore::createEdgeLabel(const std::string& name,
                                                                    const std::vector<PropertyDef>& properties) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    if (schema_.edge_label_name_to_id.count(name)) {
        spdlog::error("EdgeLabel '{}' already exists", name);
        co_return INVALID_EDGE_LABEL_ID;
    }

    EdgeLabelId id;
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        id = schema_.next_edge_label_id++;
    }
    auto& s = store_->get();
    auto& i = io_->get();

    EdgeLabelDef def;
    def.id = id;
    def.name = name;
    uint16_t prop_id = 0;
    for (const auto& p : properties) {
        PropertyDef pd = p;
        pd.id = prop_id++;
        def.properties.push_back(std::move(pd));
    }

    auto encoded = MetadataCodec::encodeEdgeLabelDef(def);
    std::string name_key = std::string("M|edge_label:") + name;
    std::string id_key = std::string("M|edge_label_id:") + std::to_string(id);

    const bool persisted =
        co_await i.dispatch([&]() { return s.metadataPut(name_key, encoded) && s.metadataPut(id_key, encoded); });
    if (!persisted)
        throw std::runtime_error("Failed to persist edge label definition '" + name + "'");
    co_await saveNextIds();

    // NOTE: No longer calls store.createEdgeLabel(id).

    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.edge_labels[id] = def;
        schema_.edge_label_name_to_id[name] = id;
    }
    bumpSchemaGeneration();
    publishSchemaVersion();

    spdlog::info("Created edge_label '{}' with id={}, {} properties", name, id, def.properties.size());
    co_return id;
}

folly::coro::Task<bool>
AsyncGraphMetaStore::addVertexLabelProperties(const std::string& name,
                                              const std::vector<std::pair<std::string, PropertyType>>& prop_defs) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    auto label_opt = schema_.getLabel(name);
    if (!label_opt.has_value()) {
        spdlog::error("Label '{}' does not exist, cannot add properties", name);
        co_return false;
    }
    LabelDef def = schema_.labels[label_opt->id];
    uint16_t next_prop_id = static_cast<uint16_t>(def.properties.size());
    for (const auto& [pname, ptype] : prop_defs) {
        if (findPropId(def.properties, pname)) {
            spdlog::warn("Property '{}' already exists on Label '{}', skipped", pname, name);
            continue;
        }
        PropertyDef pd;
        pd.id = next_prop_id++;
        pd.name = pname;
        pd.type = ptype;
        def.properties.push_back(std::move(pd));
    }

    auto& s = store_->get();
    auto& i = io_->get();
    co_await i.dispatchVoid([&]() { persistLabelDef(s, def); });
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.labels[label_opt->id] = std::move(def);
    }

    bumpSchemaGeneration();
    publishSchemaVersion();
    spdlog::info("Added {} properties to label '{}'", prop_defs.size(), name);
    co_return true;
}

folly::coro::Task<bool>
AsyncGraphMetaStore::addEdgeLabelProperties(const std::string& name,
                                            const std::vector<std::pair<std::string, PropertyType>>& prop_defs) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    auto label_opt = schema_.getEdgeLabel(name);
    if (!label_opt.has_value()) {
        spdlog::error("EdgeLabel '{}' does not exist, cannot add properties", name);
        co_return false;
    }
    EdgeLabelDef def = schema_.edge_labels[label_opt->id];
    uint16_t next_prop_id = static_cast<uint16_t>(def.properties.size());
    for (const auto& [pname, ptype] : prop_defs) {
        if (findPropId(def.properties, pname)) {
            spdlog::warn("Property '{}' already exists on EdgeLabel '{}', skipped", pname, name);
            continue;
        }
        PropertyDef pd;
        pd.id = next_prop_id++;
        pd.name = pname;
        pd.type = ptype;
        def.properties.push_back(std::move(pd));
    }

    auto& s = store_->get();
    auto& i = io_->get();
    co_await i.dispatchVoid([&]() { persistEdgeLabelDef(s, def); });
    {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.edge_labels[label_opt->id] = std::move(def);
    }

    bumpSchemaGeneration();
    publishSchemaVersion();
    spdlog::info("Added {} properties to edge_label '{}'", prop_defs.size(), name);
    co_return true;
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

namespace {
constexpr VertexId kVertexIdCacheChunk = 16384;
constexpr EdgeId kEdgeIdCacheChunk = 16384;
} // namespace

folly::coro::Task<VertexId> AsyncGraphMetaStore::nextVertexId() {
    co_return co_await nextVertexIdRange(1);
}

folly::coro::Task<EdgeId> AsyncGraphMetaStore::nextEdgeId() {
    co_return co_await nextEdgeIdRange(1);
}

folly::coro::Task<VertexId> AsyncGraphMetaStore::nextVertexIdRange(uint64_t count) {
    VertexId start;

    // Fast path: take IDs from an already-reserved in-memory cache.
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        if (vertex_cache_next_ + count <= vertex_cache_end_) {
            start = vertex_cache_next_;
            vertex_cache_next_ += count;
            co_return start;
        }
    }

    // Slow path: serialize cache refills and only persist once per chunk.
    auto refill_lock = co_await refill_mu_.co_scoped_lock();
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        if (vertex_cache_next_ + count <= vertex_cache_end_) {
            start = vertex_cache_next_;
            vertex_cache_next_ += count;
            co_return start;
        }
    }
    co_await refillVertexIdCache(count);
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        start = vertex_cache_next_;
        vertex_cache_next_ += count;
        co_return start;
    }
}

folly::coro::Task<EdgeId> AsyncGraphMetaStore::nextEdgeIdRange(uint64_t count) {
    EdgeId start;

    // Fast path: take IDs from an already-reserved in-memory cache.
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        if (edge_cache_next_ + count <= edge_cache_end_) {
            start = edge_cache_next_;
            edge_cache_next_ += count;
            co_return start;
        }
    }

    // Slow path: serialize cache refills and only persist once per chunk.
    auto refill_lock = co_await refill_mu_.co_scoped_lock();
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        if (edge_cache_next_ + count <= edge_cache_end_) {
            start = edge_cache_next_;
            edge_cache_next_ += count;
            co_return start;
        }
    }
    co_await refillEdgeIdCache(count);
    {
        std::lock_guard<std::mutex> lock(id_mu_);
        start = edge_cache_next_;
        edge_cache_next_ += count;
        co_return start;
    }
}

// ==================== __anon__ lightweight prop allocation ====================

folly::coro::Task<uint16_t> AsyncGraphMetaStore::getOrCreateAnonPropId(const std::string& prop_name,
                                                                       PropertyType prop_type) {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    // Find __anon__ label in schema
    LabelId anon_id = INVALID_LABEL_ID;
    for (const auto& [lid, ldef] : schema_.labels) {
        if (ldef.name == kAnonLabelName) {
            anon_id = lid;
            break;
        }
    }
    if (anon_id == INVALID_LABEL_ID) {
        spdlog::error("__anon__ label not found in schema");
        co_return 0;
    }

    LabelDef def = schema_.labels[anon_id];

    {
        auto lock = co_await anon_prop_mu_.co_scoped_lock();
        // LabelDef is the primary source of truth for property IDs.
        // The cache was removed: stale entries from a previous
        // scenario could persist a property-id order that differs from
        // the LabelDef, causing display to deviate from creation order.

        // Check if property already exists in LabelDef
        for (const auto& pd : def.properties) {
            if (pd.name == prop_name)
                co_return pd.id;
        }

        // Allocate new prop_id — LabelDef order preserves creation order.
        uint16_t new_id = static_cast<uint16_t>(def.properties.size());
        PropertyDef pd;
        pd.id = new_id;
        pd.name = prop_name;
        pd.type = prop_type;
        def.properties.push_back(std::move(pd));

        auto& s = store_->get();
        auto& i = io_->get();
        co_await i.dispatchVoid([&s, &def]() { persistLabelDef(s, def); });
        {
            std::unique_lock<std::shared_mutex> lock(schema_mu_);
            schema_.labels[anon_id] = std::move(def);
        }

        bumpSchemaGeneration();
        publishSchemaVersion();
        co_return new_id;
    }
}

// ==================== Private ====================

folly::coro::Task<void> AsyncGraphMetaStore::refillVertexIdCache(uint64_t count) {
    VertexId cache_start;
    VertexId chunk;
    {
        std::unique_lock<std::shared_mutex> schema_lock(schema_mu_);
        std::lock_guard<std::mutex> lock(id_mu_);
        if (vertex_cache_next_ + count <= vertex_cache_end_)
            co_return;
        chunk = count > kVertexIdCacheChunk ? count : kVertexIdCacheChunk;
        cache_start = schema_.next_vertex_id;
        schema_.next_vertex_id += chunk;
    }

    // Persist the new high-water mark before exposing the cached range to callers.
    co_await saveNextIds();

    {
        std::lock_guard<std::mutex> lock(id_mu_);
        vertex_cache_next_ = cache_start;
        vertex_cache_end_ = cache_start + chunk;
    }
}

folly::coro::Task<void> AsyncGraphMetaStore::refillEdgeIdCache(uint64_t count) {
    EdgeId cache_start;
    EdgeId chunk;
    {
        std::unique_lock<std::shared_mutex> schema_lock(schema_mu_);
        std::lock_guard<std::mutex> lock(id_mu_);
        if (edge_cache_next_ + count <= edge_cache_end_)
            co_return;
        chunk = count > kEdgeIdCacheChunk ? count : kEdgeIdCacheChunk;
        cache_start = schema_.next_edge_id;
        schema_.next_edge_id += chunk;
    }

    // Persist the new high-water mark before exposing the cached range to callers.
    co_await saveNextIds();

    {
        std::lock_guard<std::mutex> lock(id_mu_);
        edge_cache_next_ = cache_start;
        edge_cache_end_ = cache_start + chunk;
    }
}

namespace {

std::string encodeCleanupTables(const std::vector<std::string>& tables) {
    std::string out;
    const uint16_t count = static_cast<uint16_t>(tables.size());
    out.push_back(static_cast<char>(count >> 8));
    out.push_back(static_cast<char>(count & 0xFF));
    for (const auto& table : tables) {
        const uint16_t len = static_cast<uint16_t>(table.size());
        out.push_back(static_cast<char>(len >> 8));
        out.push_back(static_cast<char>(len & 0xFF));
        out.append(table);
    }
    return out;
}

std::vector<std::string> decodeCleanupTables(std::string_view data) {
    std::vector<std::string> tables;
    if (data.size() < 2)
        return tables;
    const auto count = static_cast<uint16_t>((static_cast<uint16_t>(static_cast<uint8_t>(data[0])) << 8) |
                                             static_cast<uint8_t>(data[1]));
    size_t offset = 2;
    for (uint16_t i = 0; i < count; ++i) {
        if (offset + 2 > data.size())
            break;
        const auto len = static_cast<uint16_t>((static_cast<uint16_t>(static_cast<uint8_t>(data[offset])) << 8) |
                                               static_cast<uint8_t>(data[offset + 1]));
        offset += 2;
        if (offset + len > data.size())
            break;
        tables.emplace_back(data.substr(offset, len));
        offset += len;
    }
    return tables;
}

std::string cleanupKey(const std::string& kind, uint32_t index_id) {
    return "M|cleanup:" + kind + ":" + std::to_string(index_id);
}

} // namespace

folly::coro::Task<bool> AsyncGraphMetaStore::putCleanupRecord(const std::string& kind, uint32_t index_id,
                                                              const std::vector<std::string>& tables) {
    auto value = encodeCleanupTables(tables);
    const auto key = cleanupKey(kind, index_id);
    co_return co_await io_->get().dispatch([this, &key, &value]() { return store_->get().metadataPut(key, value); });
}

folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexCleanupRecord>> AsyncGraphMetaStore::listCleanupRecords() {
    co_return co_await io_->get().dispatch([this]() {
        std::vector<IndexCleanupRecord> records;
        store_->get().metadataScan("M|cleanup:", [&](std::string_view key, std::string_view value) {
            // M|cleanup:<kind>:<id>
            const std::string_view rest = key.substr(10);
            const auto colon = rest.rfind(':');
            if (colon == std::string_view::npos)
                return true;
            IndexCleanupRecord record;
            record.kind = std::string(rest.substr(0, colon));
            try {
                record.index_id = static_cast<uint32_t>(std::stoul(std::string(rest.substr(colon + 1))));
            } catch (...) {
                return true;
            }
            record.tables = decodeCleanupTables(value);
            records.push_back(std::move(record));
            return true;
        });
        return records;
    });
}

folly::coro::Task<bool> AsyncGraphMetaStore::removeCleanupRecord(const std::string& kind, uint32_t index_id) {
    const auto key = cleanupKey(kind, index_id);
    co_return co_await io_->get().dispatch([this, &key]() { return store_->get().metadataDel(key); });
}

folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexCleanupRecord>>
AsyncGraphMetaStore::recoverUnfinishedIndexes() {
    // 构建任务只活在进程内存里；重启后 WRITE_ONLY 不可能继续，也没有扫描水位可以续建。
    // 安全方向：一律 ERROR（绝不 PUBLIC），保留定义供用户 DROP 后重建；
    // 同时把半成品表登记为 orphan（调用方在启动时尝试删除，失败则留到下次启动）。
    std::vector<IndexCleanupRecord> records;
    // 不能持有 schema_ 的引用跨 co_await：updateIndexState 会替换 map element，
    // 导致外层 idx/label 悬空。先拷贝快照，再按值处理。
    const GraphSchema snapshot = schemaSnapshot();

    for (const auto& [lid, label] : snapshot.labels) {
        for (const auto& idx : label.indexes) {
            if (idx.state != IndexState::WRITE_ONLY)
                continue;
            const std::string index_name = idx.name;
            if (co_await updateIndexState(index_name, IndexState::ERROR))
                spdlog::warn("[index] '{}' was BUILDING at shutdown => ERROR (no crash resume; drop and rebuild)",
                             index_name);
            else
                spdlog::error("[index] failed to mark unfinished index '{}' as ERROR", index_name);

            IndexCleanupRecord record;
            record.kind = "orphan";
            record.index_id = idx.index_id;
            record.tables = {vidxTableById(idx.index_id), indexDeltaTable(idx.index_id)};
            co_await putCleanupRecord(record.kind, record.index_id, record.tables);
            records.push_back(std::move(record));
        }
    }
    for (const auto& [elid, elabel] : snapshot.edge_labels) {
        for (const auto& idx : elabel.indexes) {
            if (idx.state != IndexState::WRITE_ONLY)
                continue;
            const std::string index_name = idx.name;
            if (co_await updateIndexState(index_name, IndexState::ERROR))
                spdlog::warn("[index] '{}' was BUILDING at shutdown => ERROR (no crash resume; drop and rebuild)",
                             index_name);
            else
                spdlog::error("[index] failed to mark unfinished index '{}' as ERROR", index_name);

            IndexCleanupRecord record;
            record.kind = "orphan";
            record.index_id = idx.index_id;
            record.tables = {eidxCompositeTable(elid, idx.prop_ids), indexDeltaTable(idx.index_id)};
            co_await putCleanupRecord(record.kind, record.index_id, record.tables);
            records.push_back(std::move(record));
        }
    }
    co_return records;
}

folly::coro::Task<size_t> AsyncGraphMetaStore::repairMissingIndexIds() {
    auto schema_write_lock = co_await schema_write_mu_.co_scoped_lock();
    // 一次性修复：旧版本 createEdgeIndex 没分配 index_id ⇒ 持久化为 0。
    // 0 无法注册/路由（也没法区分"不存在"），这里按单调计数器补一个并持久化定义。
    // 幂等：只处理 index_id==0；修完再跑是 no-op。
    size_t repaired = 0;
    auto& s = store_->get();
    auto& i = io_->get();

    for (auto& [lid, label] : schema_.labels) {
        bool changed = false;
        for (auto& idx : label.indexes) {
            if (idx.index_id == 0) {
                idx.index_id = schema_.next_index_id++;
                changed = true;
                ++repaired;
            }
        }
        if (!changed)
            continue;
        const LabelDef snapshot = label;
        co_await i.dispatchVoid([&s, &snapshot]() { persistLabelDef(s, snapshot); });
    }

    for (auto& [elid, elabel] : schema_.edge_labels) {
        bool changed = false;
        for (auto& idx : elabel.indexes) {
            if (idx.index_id == 0) {
                idx.index_id = schema_.next_index_id++;
                changed = true;
                ++repaired;
            }
        }
        if (!changed)
            continue;
        const EdgeLabelDef snapshot = elabel;
        co_await i.dispatchVoid([&s, &snapshot]() { persistEdgeLabelDef(s, snapshot); });
    }

    if (repaired > 0)
        co_await saveNextIds();
    co_return repaired;
}

folly::coro::Task<void> AsyncGraphMetaStore::saveNextIds() {
    const bool persisted = co_await io_->get().dispatch([this]() {
        std::shared_lock<std::shared_mutex> schema_lock(schema_mu_);
        std::lock_guard<std::mutex> lock(id_mu_);
        auto encoded = MetadataCodec::encodeNextIds(schema_.next_vertex_id, schema_.next_edge_id, schema_.next_label_id,
                                                    schema_.next_edge_label_id, schema_.next_index_id);
        return store_->get().metadataPut("M|next_ids", encoded);
    });
    if (!persisted)
        throw std::runtime_error("Failed to persist next ids");
}

} // namespace eugraph
