#include "storage/data/sync_graph_data_store.hpp"

#include "storage/data/graph_session.hpp"
#include "storage/wt_table_ops.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include "storage/kv/index_key_codec.hpp"
#include "storage/store_profile.hpp"

namespace eugraph {

namespace {
constexpr uint32_t kAdjCacheCapacity = 0;
} // namespace

// ==================== WT Item Helpers (local) ====================

namespace {

void setItem(WT_CURSOR* cursor, std::string_view data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_key(cursor, &item);
}

std::string getValueFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_value(cursor, &item);
    return std::string(static_cast<const char*>(item.data), item.size);
}

std::string getKeyFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_key(cursor, &item);
    return std::string(static_cast<const char*>(item.data), item.size);
}

std::string_view getKeyViewFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_key(cursor, &item);
    return std::string_view(static_cast<const char*>(item.data), item.size);
}

std::string_view getValueViewFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_value(cursor, &item);
    return std::string_view(static_cast<const char*>(item.data), item.size);
}

int getKeyChecked(WT_CURSOR* cursor, std::string& out) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    const int ret = cursor->get_key(cursor, &item);
    if (ret == 0)
        out.assign(static_cast<const char*>(item.data), item.size);
    return ret;
}

int getValueChecked(WT_CURSOR* cursor, std::string& out) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    const int ret = cursor->get_value(cursor, &item);
    if (ret == 0)
        out.assign(static_cast<const char*>(item.data), item.size);
    return ret;
}

/// 索引 key = sortable-value + 8 字节 entity_id；区间上界比较需剥掉该后缀 ✓
std::string_view stripEntityIdSuffix(std::string_view key) {
    return key.size() >= 8 ? key.substr(0, key.size() - 8) : key;
}

} // anonymous namespace

// ==================== DDL ====================

bool SyncGraphDataStore::ensure_tables() {
    if (!isOpen()) {
        spdlog::error("SyncGraphDataStore::ensure_tables: connection not bound");
        return false;
    }
    WtSession session = conn().make_session();
    return schema_ddl_.ensure_tables(session, {TABLE_VERTEX_DATA, TABLE_EDGE_INDEX, TABLE_VERTEX_EXISTENCE});
}

bool SyncGraphDataStore::createLabel(LabelId label_id) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();
    return schema_ddl_.create_tables(session, {labelFwdTable(label_id)});
}

bool SyncGraphDataStore::dropLabel(LabelId label_id) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();
    const std::string label_fwd = labelFwdTable(label_id);

    // Enumerate this label's vertices via label_fwd, then delete their
    // vertex_data label/property records. This avoids a full vertex_data scan.
    std::vector<VertexId> vids;
    wt::table_scan(session.get(), label_fwd, {}, [&](std::string_view key, std::string_view) {
        vids.push_back(KeyCodec::decodeLabelForwardKey(key));
        return true;
    });
    for (VertexId vid : vids) {
        wt::table_del(session.get(), TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataLabelKey(vid, label_id));
        std::vector<std::string> prop_keys;
        wt::table_scan(session.get(), TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataPropPrefix(vid, label_id),
                       [&](std::string_view key, std::string_view) {
                           prop_keys.emplace_back(key);
                           return true;
                       });
        for (const auto& key : prop_keys)
            wt::table_del(session.get(), TABLE_VERTEX_DATA, key);
    }
    return schema_ddl_.drop_tables(session, {label_fwd}, SchemaDdl::DropMode::CheckpointFirst);
}

bool SyncGraphDataStore::createEdgeLabel(EdgeLabelId edge_label_id) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();
    return schema_ddl_.create_tables(session, {etypeTable(edge_label_id), epropTable(edge_label_id)});
}

bool SyncGraphDataStore::dropEdgeLabel(EdgeLabelId edge_label_id) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();

    // Remove this label's adjacency entries from vertex_data/edge_index before
    // dropping the per-label tables. etype enumerates every edge of the label.
    std::vector<KeyCodec::EdgeTypeIndexKey> edges;
    wt::table_scan(session.get(), etypeTable(edge_label_id), {}, [&](std::string_view key, std::string_view) {
        edges.push_back(KeyCodec::decodeEdgeTypeIndexKey(key));
        return true;
    });
    for (const auto& e : edges) {
        auto drop_one = [&](VertexId vid, Direction dir, VertexId neighbor) {
            auto st = getAdjacencyState(session.get(), vid, dir);
            if (st && st->state == KeyCodec::AdjacencyCacheState::COMPLETE) {
                KeyCodec::VertexDataAdjEdgeKey ck{vid, dir, edge_label_id, neighbor, e.seq};
                wt::table_del(session.get(), TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjEdgeKey(ck));
                uint32_t count = st->count > 0 ? st->count - 1 : 0;
                putAdjacencyState(session.get(), vid, dir, KeyCodec::AdjacencyCacheState::COMPLETE, count);
            } else {
                KeyCodec::EdgeIndexKey ik{vid, dir, edge_label_id, neighbor, e.seq};
                wt::table_del(session.get(), TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(ik));
            }
        };
        drop_one(e.src_vertex_id, Direction::OUT, e.dst_vertex_id);
        drop_one(e.dst_vertex_id, Direction::IN, e.src_vertex_id);
    }

    return schema_ddl_.drop_tables(session, {etypeTable(edge_label_id), epropTable(edge_label_id)},
                                   SchemaDdl::DropMode::CheckpointFirst);
}

bool SyncGraphDataStore::insertVertex(GraphSession& gs, VertexId vid,
                                      std::span<const std::pair<LabelId, Properties>> label_props) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto ve_key = KeyCodec::encodeVertexExistenceKey(vid);
    if (!wt::table_put(gs, TABLE_VERTEX_EXISTENCE, ve_key, {}))
        return false;

    if (kAdjCacheCapacity > 0) {
        for (Direction dir : {Direction::OUT, Direction::IN}) {
            if (!wt::table_put(gs, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjStateKey(vid, dir),
                               KeyCodec::encodeAdjacencyState(KeyCodec::AdjacencyCacheState::COMPLETE, 0)))
                return false;
        }
    }

    for (const auto& [label_id, props] : label_props) {
        if (label_id == INVALID_LABEL_ID)
            continue;
        auto lr_key = KeyCodec::encodeVertexDataLabelKey(vid, label_id);
        if (!wt::table_put(gs, TABLE_VERTEX_DATA, lr_key, {}))
            return false;

        auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
        if (!wt::table_put(gs, labelFwdTable(label_id), lf_key, {}))
            return false;

        for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
            if (props[prop_id].has_value()) {
                auto vp_key = KeyCodec::encodeVertexDataPropKey(vid, label_id, prop_id);
                auto vp_val = ValueCodec::encode(props[prop_id].value());
                if (!wt::table_put(gs, TABLE_VERTEX_DATA, vp_key, vp_val))
                    return false;
            }
        }
    }

    return true;
}

bool SyncGraphDataStore::deleteVertex(GraphSession& gs, VertexId vid) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto labels = getVertexLabels(gs, vid);
    for (LabelId label_id : labels) {
        wt::table_del(gs, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataLabelKey(vid, label_id));
        wt::table_del(gs, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

        auto prefix = KeyCodec::encodeVertexDataPropPrefix(vid, label_id);
        wt::table_scan(gs, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view) {
            wt::table_del(gs, TABLE_VERTEX_DATA, key);
            return true;
        });
    }

    // Remove every remaining vertex_data record (state + cached adjacency).
    std::vector<std::string> data_keys;
    wt::table_scan(gs, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataVertexPrefix(vid),
                   [&](std::string_view key, std::string_view) {
                       data_keys.emplace_back(key);
                       return true;
                   });
    for (const auto& key : data_keys)
        wt::table_del(gs, TABLE_VERTEX_DATA, key);

    wt::table_del(gs, TABLE_VERTEX_EXISTENCE, KeyCodec::encodeVertexExistenceKey(vid));

    return true;
}

// ==================== Vertex Properties ====================

std::optional<Properties> SyncGraphDataStore::getVertexProperties(GraphSession& gs, VertexId vid, LabelId label_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return std::nullopt;

    auto prefix = KeyCodec::encodeVertexDataPropPrefix(vid, label_id);
    Properties props;
    bool found_any = false;

    wt::table_scan(gs, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view value) {
        auto [_, __, prop_id] = KeyCodec::decodeVertexDataPropKey(key);
        if (prop_id >= props.size())
            props.resize(prop_id + 1);
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any)
        return std::nullopt;
    return props;
}

std::vector<std::optional<Properties>> SyncGraphDataStore::getVertexPropertiesBatchProjected(
    GraphSession& gs, LabelId label_id, const std::vector<VertexId>& vids, const std::vector<uint16_t>& proj) {
    std::vector<std::optional<Properties>> out(vids.size());
    if (proj.empty())
        return out;
    if (vids.empty())
        return out;
    // One cursor for the whole batch (see the header comment for the measured rationale).
    // 同一个 vid 在批内重复很常见（见 getVertexLabelsBatch）：只按唯一 vid 扫。
    std::vector<size_t> unique_of(vids.size());
    std::vector<VertexId> unique;
    unique.reserve(vids.size());
    std::unordered_map<VertexId, size_t> seen;
    seen.reserve(vids.size() * 2);
    for (size_t i = 0; i < vids.size(); ++i) {
        auto [it, inserted] = seen.try_emplace(vids[i], unique.size());
        if (inserted)
            unique.push_back(vids[i]);
        unique_of[i] = it->second;
    }
    const bool all_unique = unique.size() == vids.size();
    std::vector<std::optional<Properties>> unique_out(all_unique ? 0 : unique.size());
    const std::string table = TABLE_VERTEX_DATA;
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "getVertexPropertiesBatchProjected: cannot open cursor on " + table);
        return out;
    }
    auto* c = cursor.get();
    const size_t width = static_cast<size_t>(*std::max_element(proj.begin(), proj.end())) + 1;
    for (size_t i = 0; i < unique.size(); ++i) {
        std::optional<Properties>& slot = all_unique ? out[i] : unique_out[i];
        Properties props;
        props.resize(width);
        bool found = false;
        for (uint16_t pid : proj) {
            auto key = KeyCodec::encodeVertexDataPropKey(unique[i], label_id, pid);
            setItem(c, key);
            const int ret = c->search(c);
            if (ret == WT_NOTFOUND)
                continue;
            if (ret != 0) {
                gs.fail_from_wt(ret, "getVertexPropertiesBatchProjected: search failed");
                return out;
            }
            props[pid] = ValueCodec::decode(getValueFromCursor(c));
            found = true;
        }
        if (found)
            slot = std::move(props);
    }
    if (!all_unique) {
        for (size_t i = 0; i < vids.size(); ++i)
            out[i] = unique_out[unique_of[i]];
    }
    return out;
}

std::optional<PropertyValue> SyncGraphDataStore::getVertexProperty(GraphSession& gs, VertexId vid, LabelId label_id,
                                                                   uint16_t prop_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return std::nullopt;

    auto key = KeyCodec::encodeVertexDataPropKey(vid, label_id, prop_id);
    auto val = wt::table_get(gs, TABLE_VERTEX_DATA, key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

bool SyncGraphDataStore::putVertexProperty(GraphSession& gs, VertexId vid, LabelId label_id, uint16_t prop_id,
                                           const PropertyValue& value) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto key = KeyCodec::encodeVertexDataPropKey(vid, label_id, prop_id);
    return wt::table_put(gs, TABLE_VERTEX_DATA, key, ValueCodec::encode(value));
}

bool SyncGraphDataStore::putVertexProperties(GraphSession& gs, VertexId vid, LabelId label_id,
                                             const Properties& props) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto key = KeyCodec::encodeVertexDataPropKey(vid, label_id, prop_id);
            if (!wt::table_put(gs, TABLE_VERTEX_DATA, key, ValueCodec::encode(props[prop_id].value())))
                return false;
        }
    }
    return true;
}

bool SyncGraphDataStore::deleteVertexProperty(GraphSession& gs, VertexId vid, LabelId label_id, uint16_t prop_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto key = KeyCodec::encodeVertexDataPropKey(vid, label_id, prop_id);
    return wt::table_del(gs, TABLE_VERTEX_DATA, key);
}

// ==================== Vertex Labels ====================

LabelIdSet SyncGraphDataStore::getVertexLabels(GraphSession& gs, VertexId vid) {
    LabelIdSet labels;
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return labels;

    auto prefix = KeyCodec::encodeVertexDataLabelPrefix(vid);
    wt::table_scan(gs, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view) {
        auto [_, label_id] = KeyCodec::decodeVertexDataLabelKey(key);
        labels.insert(label_id);
        return true;
    });
    return labels;
}

std::vector<LabelIdSet> SyncGraphDataStore::getVertexLabelsBatch(GraphSession& gs, const std::vector<VertexId>& vids) {
    StoreProfileScope prof("getVertexLabelsBatch", vids.size());
    std::vector<LabelIdSet> out(vids.size());
    if (vids.empty())
        return out;
    // 批内同一个 vid 很常见（一个已绑定顶点被 Expand 复制到整批行，如 complex-3 的
    // friend/countryX/countryY）。按唯一 vid 扫一次，再映射回原顺序：WT 的每个
    // search_near 都不便宜，1024 个重复 id 做 1024 次扫描是纯浪费。
    std::vector<size_t> unique_of(vids.size());
    std::vector<VertexId> unique;
    unique.reserve(vids.size());
    std::unordered_map<VertexId, size_t> seen;
    seen.reserve(vids.size() * 2);
    for (size_t i = 0; i < vids.size(); ++i) {
        auto [it, inserted] = seen.try_emplace(vids[i], unique.size());
        if (inserted)
            unique.push_back(vids[i]);
        unique_of[i] = it->second;
    }
    const bool all_unique = unique.size() == vids.size();
    std::vector<LabelIdSet> unique_out(all_unique ? 0 : unique.size());
    CursorHandle cursor = gs.acquire_scan_cursor(TABLE_VERTEX_DATA);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, std::string("getVertexLabelsBatch: cannot open cursor on ") + TABLE_VERTEX_DATA);
        return out;
    }
    auto* c = cursor.get();
    std::string prefix;
    for (size_t i = 0; i < unique.size(); ++i) {
        LabelIdSet& labels = all_unique ? out[i] : unique_out[i];
        prefix = KeyCodec::encodeVertexDataLabelPrefix(unique[i]);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND)
            continue;
        if (ret != 0) {
            gs.fail_from_wt(ret, "getVertexLabelsBatch: search_near failed");
            return out;
        }
        if (exact < 0)
            ret = c->next(c);
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break; // 正常结束：下一个 vid 的 key（ret == 0，不是错误）✓
            auto [_, label_id] = KeyCodec::decodeVertexDataLabelKey(key);
            labels.insert(label_id);
            ret = c->next(c);
        }
        if (ret != 0 && ret != WT_NOTFOUND) {
            gs.fail_from_wt(ret, "getVertexLabelsBatch: next failed");
            return out;
        }
    }
    if (!all_unique) {
        for (size_t i = 0; i < vids.size(); ++i)
            out[i] = unique_out[unique_of[i]];
    }
    return out;
}

std::vector<std::optional<Properties>> SyncGraphDataStore::getVertexPropertiesBatch(GraphSession& gs, LabelId label_id,
                                                                                    const std::vector<VertexId>& vids) {
    std::vector<std::optional<Properties>> out(vids.size());
    if (vids.empty())
        return out;
    // 同一个 vid 在批内重复很常见（见 getVertexLabelsBatch）：只按唯一 vid 扫。
    std::vector<size_t> unique_of(vids.size());
    std::vector<VertexId> unique;
    unique.reserve(vids.size());
    std::unordered_map<VertexId, size_t> seen;
    seen.reserve(vids.size() * 2);
    for (size_t i = 0; i < vids.size(); ++i) {
        auto [it, inserted] = seen.try_emplace(vids[i], unique.size());
        if (inserted)
            unique.push_back(vids[i]);
        unique_of[i] = it->second;
    }
    const bool all_unique = unique.size() == vids.size();
    std::vector<std::optional<Properties>> unique_out(all_unique ? 0 : unique.size());
    const std::string table = TABLE_VERTEX_DATA;
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "getVertexPropertiesBatch: cannot open cursor on " + table);
        return out;
    }
    auto* c = cursor.get();
    std::string prefix;
    for (size_t i = 0; i < unique.size(); ++i) {
        std::optional<Properties>& slot = all_unique ? out[i] : unique_out[i];
        prefix = KeyCodec::encodeVertexDataPropPrefix(unique[i], label_id);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND)
            continue;
        if (ret != 0) {
            gs.fail_from_wt(ret, "getVertexPropertiesBatch: search_near failed");
            return out;
        }
        if (exact < 0)
            ret = c->next(c);
        Properties props;
        bool found = false;
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break; // 正常结束：下一个 vid 的 key（ret == 0，不是错误）✓
            auto [_, __, prop_id] = KeyCodec::decodeVertexDataPropKey(key);
            if (prop_id >= props.size())
                props.resize(prop_id + 1);
            props[prop_id] = ValueCodec::decode(getValueFromCursor(c));
            found = true;
            ret = c->next(c);
        }
        if (ret != 0 && ret != WT_NOTFOUND) {
            gs.fail_from_wt(ret, "getVertexPropertiesBatch: next failed");
            return out;
        }
        if (found)
            slot = std::move(props);
    }
    if (!all_unique) {
        for (size_t i = 0; i < vids.size(); ++i)
            out[i] = unique_out[unique_of[i]];
    }
    return out;
}

std::vector<std::vector<PropertyValue>>
SyncGraphDataStore::getVertexCoalescePropertiesBatch(GraphSession& gs, const std::vector<VertexId>& vids,
                                                     const std::vector<std::pair<LabelId, uint16_t>>& candidates) {
    StoreProfileScope prof("getVertexCoalescePropertiesBatch", vids.size());
    std::vector<std::vector<PropertyValue>> out(vids.size());
    if (vids.empty() || candidates.empty())
        return out;

    std::unordered_map<uint32_t, size_t> cand_index;
    cand_index.reserve(candidates.size() * 2);
    for (size_t i = 0; i < candidates.size(); ++i) {
        uint32_t key = (static_cast<uint32_t>(candidates[i].first) << 16) | candidates[i].second;
        cand_index.emplace(key, i);
    }

    CursorHandle cursor = gs.acquire_scan_cursor(TABLE_VERTEX_DATA);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "getVertexCoalescePropertiesBatch: cannot open cursor on table:vertex_data");
        return out;
    }
    auto* c = cursor.get();
    size_t out_index = 0;
    for (VertexId vid : vids) {
        std::vector<std::optional<PropertyValue>> found(candidates.size());
        std::string prefix = KeyCodec::encodeVertexDataPropPrefix(vid);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND) {
            ++out_index;
            continue;
        }
        if (ret != 0) {
            gs.fail_from_wt(ret, "getVertexCoalescePropertiesBatch: search_near failed");
            return out;
        }
        if (exact < 0)
            ret = c->next(c);
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break;
            auto [_, label_id, prop_id] = KeyCodec::decodeVertexDataPropKey(key);
            uint32_t key_id = (static_cast<uint32_t>(label_id) << 16) | prop_id;
            auto it = cand_index.find(key_id);
            if (it != cand_index.end())
                found[it->second] = ValueCodec::decode(getValueFromCursor(c));
            ret = c->next(c);
        }
        if (ret != 0 && ret != WT_NOTFOUND) {
            gs.fail_from_wt(ret, "getVertexCoalescePropertiesBatch: next failed");
            return out;
        }
        for (auto& v : found)
            if (v)
                out[out_index].push_back(std::move(*v));
        ++out_index;
    }
    return out;
}

bool SyncGraphDataStore::addVertexLabel(GraphSession& gs, VertexId vid, LabelId label_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto lr_key = KeyCodec::encodeVertexDataLabelKey(vid, label_id);
    if (!wt::table_put(gs, TABLE_VERTEX_DATA, lr_key, {}))
        return false;

    auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
    return wt::table_put(gs, labelFwdTable(label_id), lf_key, {});
}

bool SyncGraphDataStore::removeVertexLabel(GraphSession& gs, VertexId vid, LabelId label_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    wt::table_del(gs, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataLabelKey(vid, label_id));
    wt::table_del(gs, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

    auto prefix = KeyCodec::encodeVertexDataPropPrefix(vid, label_id);
    wt::table_scan(gs, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view) {
        wt::table_del(gs, TABLE_VERTEX_DATA, key);
        return true;
    });

    return true;
}

// ==================== Label Index Scan ====================

void SyncGraphDataStore::scanVerticesByLabel(GraphSession& gs, LabelId label_id,
                                             const std::function<bool(VertexId)>& callback) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return;

    wt::table_scan(gs, labelFwdTable(label_id), {}, [&](std::string_view key, std::string_view) {
        VertexId vid = KeyCodec::decodeLabelForwardKey(key);
        return callback(vid);
    });
}

// ==================== All-Vertex Scan ====================

void SyncGraphDataStore::scanAllVertices(GraphSession& gs, const std::function<bool(VertexId)>& callback) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return;

    wt::table_scan(gs, TABLE_VERTEX_EXISTENCE, {}, [&](std::string_view key, std::string_view) {
        VertexId vid = KeyCodec::decodeVertexExistenceKey(key);
        return callback(vid);
    });
}

// ==================== Edge ====================

std::optional<SyncGraphDataStore::AdjState> SyncGraphDataStore::getAdjacencyState(WT_SESSION* session, VertexId vid,
                                                                                  Direction direction) {
    auto val = wt::table_get(session, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjStateKey(vid, direction));
    if (!val)
        return std::nullopt;
    auto [state, count] = KeyCodec::decodeAdjacencyState(*val);
    return AdjState{state, count};
}

bool SyncGraphDataStore::putAdjacencyState(WT_SESSION* session, VertexId vid, Direction direction,
                                           KeyCodec::AdjacencyCacheState state, uint32_t count) {
    return wt::table_put(session, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjStateKey(vid, direction),
                         KeyCodec::encodeAdjacencyState(state, count));
}

bool SyncGraphDataStore::promoteAdjacency(WT_SESSION* session, VertexId vid, Direction direction,
                                          uint32_t cached_count) {
    (void)cached_count;
    const std::string prefix = KeyCodec::encodeVertexDataAdjPrefix(vid, direction);
    std::vector<std::string> keys;
    std::vector<std::pair<std::string, std::string>> entries; // edge_index key -> edge_id value
    wt::table_scan(session, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view value) {
        if (KeyCodec::isVertexDataAdjStateKey(key))
            return true;
        auto k = KeyCodec::decodeVertexDataAdjEdgeKey(key);
        KeyCodec::EdgeIndexKey idx{k.vertex_id, direction, k.edge_label_id, k.neighbor_id, k.seq};
        entries.emplace_back(KeyCodec::encodeEdgeIndexKey(idx), std::string(value));
        keys.emplace_back(key);
        return true;
    });
    for (const auto& [key, value] : entries)
        if (!wt::table_put(session, TABLE_EDGE_INDEX, key, value))
            return false;
    for (const auto& key : keys)
        wt::table_del(session, TABLE_VERTEX_DATA, key);
    return putAdjacencyState(session, vid, direction, KeyCodec::AdjacencyCacheState::OVERFLOW, 0);
}

bool SyncGraphDataStore::insertAdjacencyEdge(WT_SESSION* session, VertexId vid, Direction direction,
                                             EdgeLabelId label_id, VertexId neighbor_id, uint64_t seq, EdgeId eid) {
    const std::string edge_val = ValueCodec::encodeU64(eid);
    if (kAdjCacheCapacity == 0) {
        KeyCodec::EdgeIndexKey idx{vid, direction, label_id, neighbor_id, seq};
        return wt::table_put(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(idx), edge_val);
    }
    auto state = getAdjacencyState(session, vid, direction);
    if (!state || state->state == KeyCodec::AdjacencyCacheState::OVERFLOW) {
        KeyCodec::EdgeIndexKey idx{vid, direction, label_id, neighbor_id, seq};
        return wt::table_put(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(idx), edge_val);
    }
    if (state->count >= kAdjCacheCapacity) {
        if (!promoteAdjacency(session, vid, direction, state->count))
            return false;
        KeyCodec::EdgeIndexKey idx{vid, direction, label_id, neighbor_id, seq};
        return wt::table_put(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(idx), edge_val);
    }
    KeyCodec::VertexDataAdjEdgeKey key{vid, direction, label_id, neighbor_id, seq};
    if (!wt::table_put(session, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjEdgeKey(key), edge_val))
        return false;
    return putAdjacencyState(session, vid, direction, KeyCodec::AdjacencyCacheState::COMPLETE, state->count + 1);
}

bool SyncGraphDataStore::deleteAdjacencyEdge(WT_SESSION* session, VertexId vid, Direction direction,
                                             EdgeLabelId label_id, VertexId neighbor_id, uint64_t seq, EdgeId eid) {
    (void)eid;
    if (kAdjCacheCapacity == 0) {
        KeyCodec::EdgeIndexKey idx{vid, direction, label_id, neighbor_id, seq};
        wt::table_del(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(idx));
        return true;
    }
    auto state = getAdjacencyState(session, vid, direction);
    if (state && state->state == KeyCodec::AdjacencyCacheState::COMPLETE) {
        KeyCodec::VertexDataAdjEdgeKey key{vid, direction, label_id, neighbor_id, seq};
        wt::table_del(session, TABLE_VERTEX_DATA, KeyCodec::encodeVertexDataAdjEdgeKey(key));
        uint32_t count = state->count > 0 ? state->count - 1 : 0;
        return putAdjacencyState(session, vid, direction, KeyCodec::AdjacencyCacheState::COMPLETE, count);
    }
    KeyCodec::EdgeIndexKey idx{vid, direction, label_id, neighbor_id, seq};
    wt::table_del(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(idx));
    return true;
}

bool SyncGraphDataStore::collectCachedEdges(WT_SESSION* session, VertexId vid, Direction direction,
                                            std::optional<EdgeLabelId> label_filter,
                                            std::vector<ISyncGraphDataStore::EdgeIndexEntry>& out) {
    auto state = getAdjacencyState(session, vid, direction);
    if (!state || state->state != KeyCodec::AdjacencyCacheState::COMPLETE)
        return false;
    const std::string prefix = KeyCodec::encodeVertexDataAdjPrefix(vid, direction);
    wt::table_scan(session, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view value) {
        if (KeyCodec::isVertexDataAdjStateKey(key))
            return true;
        auto k = KeyCodec::decodeVertexDataAdjEdgeKey(key);
        if (label_filter && k.edge_label_id != *label_filter)
            return true;
        out.push_back({k.neighbor_id, k.edge_label_id, k.seq, ValueCodec::decodeU64(value)});
        return true;
    });
    return true;
}

bool SyncGraphDataStore::insertEdge(GraphSession& gs, EdgeId eid, VertexId src_id, VertexId dst_id,
                                    EdgeLabelId label_id, uint64_t seq, const Properties& props) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    const std::string edge_id_val = ValueCodec::encodeU64(eid);
    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    if (!wt::table_put(gs, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key), edge_id_val))
        return false;

    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto ep_key = KeyCodec::encodeEPropKey(eid, prop_id);
            auto ep_val = ValueCodec::encode(props[prop_id].value());
            if (!wt::table_put(gs, epropTable(label_id), ep_key, ep_val))
                return false;
        }
    }

    if (!insertAdjacencyEdge(gs.wt_session(), src_id, Direction::OUT, label_id, dst_id, seq, eid))
        return false;
    if (!insertAdjacencyEdge(gs.wt_session(), dst_id, Direction::IN, label_id, src_id, seq, eid))
        return false;
    return true;
}

bool SyncGraphDataStore::deleteEdge(GraphSession& gs, EdgeId eid, EdgeLabelId label_id, VertexId src_id,
                                    VertexId dst_id, uint64_t seq) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    if (!deleteAdjacencyEdge(gs.wt_session(), src_id, Direction::OUT, label_id, dst_id, seq, eid))
        return false;
    if (!deleteAdjacencyEdge(gs.wt_session(), dst_id, Direction::IN, label_id, src_id, seq, eid))
        return false;

    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    wt::table_del(gs, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key));

    auto prefix = KeyCodec::encodeEPropPrefix(eid);
    wt::table_scan(gs, epropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
        wt::table_del(gs, epropTable(label_id), key);
        return true;
    });

    return true;
}

// ==================== Edge Properties ====================

std::optional<Properties> SyncGraphDataStore::getEdgeProperties(GraphSession& gs, EdgeLabelId label_id, EdgeId eid) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return std::nullopt;

    auto prefix = KeyCodec::encodeEPropPrefix(eid);
    Properties props;
    bool found_any = false;

    wt::table_scan(gs, epropTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto [_, prop_id] = KeyCodec::decodeEPropKey(key);
        if (prop_id >= props.size())
            props.resize(prop_id + 1);
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any)
        return std::nullopt;
    return props;
}

std::optional<PropertyValue> SyncGraphDataStore::getEdgeProperty(GraphSession& gs, EdgeLabelId label_id, EdgeId eid,
                                                                 uint16_t prop_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return std::nullopt;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    auto val = wt::table_get(gs, epropTable(label_id), key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

std::vector<std::optional<PropertyValue>> SyncGraphDataStore::getEdgePropertyBatch(GraphSession& gs,
                                                                                   EdgeLabelId label_id,
                                                                                   const std::vector<EdgeId>& edge_ids,
                                                                                   uint16_t prop_id) {
    std::vector<std::optional<PropertyValue>> out;
    out.reserve(edge_ids.size());
    const std::string table = epropTable(label_id);
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "getEdgePropertyBatch: cannot open cursor on " + table);
        out.resize(edge_ids.size());
        return out;
    }

    auto* c = cursor.get();
    std::string key;
    for (EdgeId eid : edge_ids) {
        key = KeyCodec::encodeEPropKey(eid, prop_id);
        setItem(c, key);
        int ret = c->search(c);
        if (ret == WT_NOTFOUND) {
            out.emplace_back(std::nullopt);
            continue;
        }
        if (ret != 0) {
            gs.fail_from_wt(ret, "getEdgePropertyBatch: search failed");
            out.resize(edge_ids.size());
            return out;
        }
        out.emplace_back(ValueCodec::decode(getValueFromCursor(c)));
    }
    return out;
}

bool SyncGraphDataStore::putEdgeProperty(GraphSession& gs, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                                         const PropertyValue& value) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return wt::table_put(gs, epropTable(label_id), key, ValueCodec::encode(value));
}

bool SyncGraphDataStore::deleteEdgeProperty(GraphSession& gs, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return wt::table_del(gs, epropTable(label_id), key);
}

// ==================== Edge Traversal ====================

void SyncGraphDataStore::scanEdges(GraphSession& gs, VertexId vid, Direction direction,
                                   std::optional<EdgeLabelId> label_filter,
                                   const std::function<bool(const EdgeIndexEntry&)>& callback) {
    StoreProfileScope prof("scanEdges");
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return;

    if (direction == Direction::BOTH) {
        scanEdges(gs, vid, Direction::OUT, label_filter, callback);
        scanEdges(gs, vid, Direction::IN, label_filter, callback);
        return;
    }

    auto state = kAdjCacheCapacity > 0 ? getAdjacencyState(gs.wt_session(), vid, direction) : std::nullopt;
    if (state && state->state == KeyCodec::AdjacencyCacheState::COMPLETE) {
        const std::string prefix = KeyCodec::encodeVertexDataAdjPrefix(vid, direction);
        wt::table_scan(gs, TABLE_VERTEX_DATA, prefix, [&](std::string_view key, std::string_view value) {
            if (KeyCodec::isVertexDataAdjStateKey(key))
                return true;
            auto k = KeyCodec::decodeVertexDataAdjEdgeKey(key);
            if (label_filter && k.edge_label_id != *label_filter)
                return true;
            EdgeIndexEntry entry{k.neighbor_id, k.edge_label_id, k.seq, ValueCodec::decodeU64(value)};
            return callback(entry);
        });
        return;
    }

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    wt::table_scan(gs, TABLE_EDGE_INDEX, prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeIndexKey(key);
        EdgeIndexEntry entry{decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

void SyncGraphDataStore::scanEdgesBatch(GraphSession& gs, const std::vector<VertexId>& src_ids, Direction direction,
                                        std::optional<EdgeLabelId> label_filter,
                                        const std::function<bool(VertexId, const EdgeIndexEntry&)>& callback) {
    WT_SESSION* session = gs.wt_session();
    if (!session || src_ids.empty())
        return;
    if (direction == Direction::BOTH) {
        scanEdgesBatch(gs, src_ids, Direction::OUT, label_filter, callback);
        scanEdgesBatch(gs, src_ids, Direction::IN, label_filter, callback);
        return;
    }
    if (kAdjCacheCapacity > 0) {
        for (VertexId vid : src_ids) {
            auto cursor = createEdgeScanCursor(gs, vid, direction, label_filter);
            if (!cursor)
                continue;
            while (cursor->valid()) {
                if (!callback(vid, cursor->entry()))
                    return;
                cursor->next();
            }
        }
        return;
    }

    CursorHandle cursor = gs.acquire_scan_cursor(TABLE_EDGE_INDEX);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "scanEdgesBatch: cannot open cursor on table:edge_index");
        return;
    }
    auto* c = cursor.get();
    for (VertexId vid : src_ids) {
        const std::string prefix = label_filter ? KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter)
                                                : KeyCodec::encodeEdgeIndexPrefix(vid, direction);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND)
            continue;
        if (ret != 0) {
            gs.fail_from_wt(ret, "scanEdgesBatch: search_near failed");
            return;
        }
        if (exact < 0)
            ret = c->next(c);
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break;
            auto decoded = KeyCodec::decodeEdgeIndexKey(key);
            EdgeIndexEntry entry{decoded.neighbor_id, decoded.edge_label_id, decoded.seq,
                                 ValueCodec::decodeU64(getValueFromCursor(c))};
            if (!callback(vid, entry))
                return;
            ret = c->next(c);
        }
        if (ret != 0 && ret != WT_NOTFOUND) {
            gs.fail_from_wt(ret, "scanEdgesBatch: next failed");
            return;
        }
    }
}

void SyncGraphDataStore::scanEdgesByType(GraphSession& gs, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                         std::optional<VertexId> dst_filter,
                                         const std::function<bool(const EdgeTypeIndexEntry&)>& callback) {
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return;

    std::string prefix;
    if (src_filter && dst_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter, *dst_filter);
    } else if (src_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter);
    }

    wt::table_scan(gs, etypeTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
        EdgeTypeIndexEntry entry{decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq,
                                 ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

// ==================== Statistics ====================

uint64_t SyncGraphDataStore::countVerticesByLabel(GraphSession& gs, LabelId label_id) {
    uint64_t count = 0;
    scanVerticesByLabel(gs, label_id, [&](VertexId) {
        ++count;
        return true;
    });
    return count;
}

uint64_t SyncGraphDataStore::countDegree(GraphSession& gs, VertexId vid, Direction direction,
                                         std::optional<EdgeLabelId> label_filter) {
    if (kAdjCacheCapacity > 0 && direction != Direction::BOTH && !label_filter) {
        auto state = getAdjacencyState(gs.wt_session(), vid, direction);
        if (state && state->state == KeyCodec::AdjacencyCacheState::COMPLETE)
            return state->count;
    }
    uint64_t count = 0;
    scanEdges(gs, vid, direction, label_filter, [&](const EdgeIndexEntry&) {
        ++count;
        return true;
    });
    return count;
}

// ==================== Scan Cursor Factories ====================

std::unique_ptr<ISyncGraphDataStore::IVertexScanCursor> SyncGraphDataStore::createVertexScanCursor(GraphSession& gs,
                                                                                                   LabelId label_id) {
    if (label_id == INVALID_LABEL_ID)
        return nullptr;
    const std::string table = labelFwdTable(label_id);
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createVertexScanCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<VertexScanCursorImpl>(gs, std::move(cursor));
}

std::unique_ptr<ISyncGraphDataStore::IVertexScanCursor>
SyncGraphDataStore::createAllVertexScanCursor(GraphSession& gs) {
    const std::string table = TABLE_VERTEX_EXISTENCE;
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createAllVertexScanCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<VertexScanCursorImpl>(gs, std::move(cursor), /*existence_keys=*/true);
}

std::unique_ptr<ISyncGraphDataStore::IEdgeTypeScanCursor>
SyncGraphDataStore::createEdgeTypeScanCursor(GraphSession& gs, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                             std::optional<VertexId> dst_filter) {
    std::string prefix;
    if (src_filter && dst_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter, *dst_filter);
    } else if (src_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter);
    }

    const std::string table = etypeTable(label_id);
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createEdgeTypeScanCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<EdgeTypeScanCursorImpl>(gs, std::move(cursor), prefix);
}

// ==================== VertexScanCursorImpl ====================

VertexScanCursorImpl::VertexScanCursorImpl(GraphSession& session, CursorHandle cursor, bool existence_keys)
    : session_(session), cursor_(std::move(cursor)), existence_keys_(existence_keys) {
    if (!cursor_)
        return;

    const int ret = cursor_.get()->next(cursor_.get());
    if (ret == 0) {
        readCurrent();
    } else if (ret != WT_NOTFOUND) {
        session_.fail_from_wt(ret, "VertexScanCursor: failed to position cursor");
    }
}

VertexScanCursorImpl::~VertexScanCursorImpl() = default;

void VertexScanCursorImpl::readCurrent() {
    currentKey_ = getKeyFromCursor(cursor_.get());
    current_vid_ = existence_keys_ ? KeyCodec::decodeVertexExistenceKey(currentKey_)
                                   : KeyCodec::decodeLabelForwardKey(currentKey_);
    valid_ = true;
}

bool VertexScanCursorImpl::valid() const {
    return valid_;
}
VertexId VertexScanCursorImpl::vertexId() const {
    return current_vid_;
}

void VertexScanCursorImpl::next() {
    if (!cursor_) {
        valid_ = false;
        return;
    }
    const int ret = cursor_.get()->next(cursor_.get());
    if (ret == WT_NOTFOUND) {
        valid_ = false;
        return;
    }
    if (ret != 0) {
        valid_ = false;
        session_.fail_from_wt(ret, "VertexScanCursor: next failed");
        return;
    }
    readCurrent();
}

// ==================== EdgeScanCursorImpl ====================

EdgeScanCursorImpl::EdgeScanCursorImpl(GraphSession& session, CursorHandle cursor, std::string_view prefix)
    : session_(session), cursor_(std::move(cursor)), prefix_(prefix) {
    if (!cursor_)
        return;

    auto* c = cursor_.get();
    setItem(c, prefix_);
    int exact = 0;
    int ret = c->search_near(c, &exact);
    if (ret != 0 && ret != WT_NOTFOUND) {
        session_.fail_from_wt(ret, "EdgeScanCursor: search_near failed");
        return;
    }
    if (ret != 0 || exact < 0) {
        ret = c->next(c);
    }

    if (ret == 0) {
        readCurrent();
    } else if (ret != WT_NOTFOUND) {
        session_.fail_from_wt(ret, "EdgeScanCursor: failed to position cursor");
    }
}

EdgeScanCursorImpl::~EdgeScanCursorImpl() = default;

void EdgeScanCursorImpl::readCurrent() {
    auto* c = cursor_.get();
    const auto key = getKeyViewFromCursor(c);
    if (!key.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    const auto value = getValueViewFromCursor(c);
    auto decoded = KeyCodec::decodeEdgeIndexKey(key);
    current_ = {decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(value)};
    valid_ = true;
}

bool EdgeScanCursorImpl::valid() const {
    return valid_;
}
const ISyncGraphDataStore::EdgeIndexEntry& EdgeScanCursorImpl::entry() const {
    return current_;
}

void EdgeScanCursorImpl::next() {
    if (!cursor_) {
        valid_ = false;
        return;
    }
    auto* c = cursor_.get();
    const int ret = c->next(c);
    if (ret == WT_NOTFOUND) {
        valid_ = false;
        return;
    }
    if (ret != 0) {
        valid_ = false;
        session_.fail_from_wt(ret, "EdgeScanCursor: next failed");
        return;
    }
    readCurrent();
}

// BOTH-direction cursor: merges OUT and IN scans into a single iterator.
class MergedEdgeScanCursor : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    MergedEdgeScanCursor(GraphSession& session, CursorHandle first, CursorHandle second, VertexId vid,
                         std::optional<EdgeLabelId> label_filter)
        : cursor1_(session, std::move(first), makePrefix(vid, Direction::OUT, label_filter)),
          cursor2_(session, std::move(second), makePrefix(vid, Direction::IN, label_filter)), onFirst_(true) {}

    const EdgeScanCursorImpl& active() const {
        if (onFirst_ && cursor1_.valid())
            return cursor1_;
        return cursor2_;
    }
    EdgeScanCursorImpl& active() {
        if (onFirst_ && cursor1_.valid())
            return cursor1_;
        return cursor2_;
    }

    bool valid() const override {
        return active().valid();
    }
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override {
        return active().entry();
    }
    void next() override {
        auto& a = active();
        a.next();
        if (!a.valid() && onFirst_)
            onFirst_ = false;
    }

private:
    static std::string makePrefix(VertexId vid, Direction dir, std::optional<EdgeLabelId> label) {
        if (label)
            return KeyCodec::encodeEdgeIndexPrefix(vid, dir, *label);
        return KeyCodec::encodeEdgeIndexPrefix(vid, dir);
    }

    EdgeScanCursorImpl cursor1_, cursor2_;
    bool onFirst_;
};

std::unique_ptr<ISyncGraphDataStore::IEdgeScanCursor>
SyncGraphDataStore::createEdgeScanCursor(GraphSession& gs, VertexId vid, Direction direction,
                                         std::optional<EdgeLabelId> label_filter) {
    StoreProfileScope prof("createEdgeScanCursor");
    if (direction == Direction::BOTH) {
        std::vector<EdgeIndexEntry> entries;
        scanEdges(gs, vid, direction, label_filter, [&](const EdgeIndexEntry& e) {
            entries.push_back(e);
            return true;
        });
        return std::make_unique<VectorEdgeScanCursor>(std::move(entries));
    }

    if (kAdjCacheCapacity > 0) {
        CursorHandle cache_cursor = gs.acquire_scan_cursor(TABLE_VERTEX_DATA);
        if (!cache_cursor) {
            const int error = cache_cursor.error();
            gs.fail_from_wt(error, "createEdgeScanCursor: cannot open cursor on " + std::string(TABLE_VERTEX_DATA));
            return nullptr;
        }
        auto cached = std::make_unique<VertexDataEdgeScanCursor>(
            gs, std::move(cache_cursor), KeyCodec::encodeVertexDataAdjPrefix(vid, direction), label_filter);
        if (cached->cacheUsable())
            return cached;
    }

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    CursorHandle cursor = gs.acquire_scan_cursor(TABLE_EDGE_INDEX);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createEdgeScanCursor: cannot open cursor on " + std::string(TABLE_EDGE_INDEX));
        return nullptr;
    }
    return std::make_unique<EdgeScanCursorImpl>(gs, std::move(cursor), prefix);
}

// ==================== VertexDataEdgeScanCursorImpl ====================

VertexDataEdgeScanCursor::VertexDataEdgeScanCursor(GraphSession& session, CursorHandle cursor, std::string_view prefix,
                                                   std::optional<EdgeLabelId> label_filter)
    : session_(session), cursor_(std::move(cursor)), prefix_(prefix), label_filter_(label_filter) {
    if (!cursor_)
        return;
    auto* c = cursor_.get();
    setItem(c, prefix_);
    int exact = 0;
    int ret = c->search_near(c, &exact);
    if (ret == WT_NOTFOUND)
        return;
    if (ret != 0) {
        session_.fail_from_wt(ret, "VertexDataEdgeScanCursor: search_near failed");
        return;
    }
    if (exact < 0)
        ret = c->next(c);
    if (ret != 0)
        return;

    currentKey_ = getKeyFromCursor(c);
    if (!currentKey_.starts_with(prefix_)) {
        cache_usable_ = true; // COMPLETE with zero edges
        return;
    }
    if (!KeyCodec::isVertexDataAdjStateKey(currentKey_)) {
        cache_usable_ = false;
        return;
    }
    auto [state, count] = KeyCodec::decodeAdjacencyState(getValueViewFromCursor(c));
    (void)count;
    cache_usable_ = state == KeyCodec::AdjacencyCacheState::COMPLETE;
    if (!cache_usable_)
        return;

    ret = c->next(c);
    if (ret == WT_NOTFOUND) {
        valid_ = false;
        return;
    }
    if (ret != 0) {
        session_.fail_from_wt(ret, "VertexDataEdgeScanCursor: next failed");
        return;
    }
    readCurrent();
}

void VertexDataEdgeScanCursor::readCurrent() {
    if (!cursor_)
        return;
    auto* c = cursor_.get();
    while (true) {
        currentKey_ = getKeyFromCursor(c);
        if (!currentKey_.starts_with(prefix_)) {
            valid_ = false;
            return;
        }
        if (KeyCodec::isVertexDataAdjStateKey(currentKey_)) {
            const int ret = c->next(c);
            if (ret != 0) {
                valid_ = false;
                return;
            }
            continue;
        }
        auto k = KeyCodec::decodeVertexDataAdjEdgeKey(currentKey_);
        if (label_filter_ && k.edge_label_id != *label_filter_) {
            const int ret = c->next(c);
            if (ret != 0) {
                valid_ = false;
                return;
            }
            continue;
        }
        currentValue_ = getValueFromCursor(c);
        current_ = {k.neighbor_id, k.edge_label_id, k.seq, ValueCodec::decodeU64(currentValue_)};
        valid_ = true;
        return;
    }
}

void VertexDataEdgeScanCursor::next() {
    if (!cursor_ || !valid_)
        return;
    auto* c = cursor_.get();
    const int ret = c->next(c);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== EdgeTypeScanCursorImpl ====================

EdgeTypeScanCursorImpl::EdgeTypeScanCursorImpl(GraphSession& session, CursorHandle cursor, std::string_view prefix)
    : session_(session), cursor_(std::move(cursor)), prefix_(prefix) {
    if (!cursor_)
        return;

    auto* c = cursor_.get();
    int ret;
    if (prefix_.empty()) {
        ret = c->next(c);
    } else {
        setItem(c, prefix_);
        int exact = 0;
        ret = c->search_near(c, &exact);
        if (ret != 0 && ret != WT_NOTFOUND) {
            session_.fail_from_wt(ret, "EdgeTypeScanCursor: search_near failed");
            return;
        }
        if (ret != 0 || exact < 0) {
            ret = c->next(c);
        }
    }

    if (ret == 0) {
        readCurrent();
    } else if (ret != WT_NOTFOUND) {
        session_.fail_from_wt(ret, "EdgeTypeScanCursor: failed to position cursor");
    }
}

EdgeTypeScanCursorImpl::~EdgeTypeScanCursorImpl() = default;

void EdgeTypeScanCursorImpl::readCurrent() {
    auto* c = cursor_.get();
    const auto key = getKeyViewFromCursor(c);
    if (!prefix_.empty() && !key.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    const auto value = getValueViewFromCursor(c);
    auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
    current_ = {decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq, ValueCodec::decodeU64(value)};
    valid_ = true;
}

bool EdgeTypeScanCursorImpl::valid() const {
    return valid_;
}
const ISyncGraphDataStore::EdgeTypeIndexEntry& EdgeTypeScanCursorImpl::entry() const {
    return current_;
}

void EdgeTypeScanCursorImpl::next() {
    if (!cursor_) {
        valid_ = false;
        return;
    }
    auto* c = cursor_.get();
    const int ret = c->next(c);
    if (ret == WT_NOTFOUND) {
        valid_ = false;
        return;
    }
    if (ret != 0) {
        valid_ = false;
        session_.fail_from_wt(ret, "EdgeTypeScanCursor: next failed");
        return;
    }
    readCurrent();
}

// ==================== Index DDL ====================

bool SyncGraphDataStore::createIndex(const std::string& table_name) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();
    return schema_ddl_.ensure_tables(session, {table_name});
}

bool SyncGraphDataStore::dropIndex(const std::string& table_name) {
    if (!isOpen())
        return false;
    WtSession session = conn().make_session();
    // ENOENT 视为成功 ⇒ drop 幂等 ✓
    return schema_ddl_.drop_tables(session, {table_name});
}

// ==================== Index Entry Operations ====================

bool SyncGraphDataStore::insertIndexEntry(GraphSession& gs, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_put(gs, table, key, {});
}

bool SyncGraphDataStore::deleteIndexEntry(GraphSession& gs, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_del(gs, table, key);
}

bool SyncGraphDataStore::insertIndexEntry(GraphSession& gs, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id, std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_put(gs, table, key, payload);
}

bool SyncGraphDataStore::insertIndexEntry(GraphSession& gs, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_put(gs, table, key, {});
}

bool SyncGraphDataStore::insertIndexEntry(GraphSession& gs, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id,
                                          std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_put(gs, table, key, payload);
}

bool SyncGraphDataStore::deleteIndexEntry(GraphSession& gs, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    return wt::table_del(gs, table, key);
}

bool SyncGraphDataStore::checkUniqueConstraint(GraphSession& gs, const std::string& table, const PropertyValue& value) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    bool found = false;
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        if (key.size() >= prefix.size()) {
            found = true;
            return false;
        }
        return true;
    });
    return !found;
}

bool SyncGraphDataStore::checkUniqueConstraint(GraphSession& gs, const std::string& table,
                                               const std::vector<PropertyValue>& values) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    WT_SESSION* session = gs.wt_session();
    if (!session)
        return false;
    bool found = false;
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        if (key.size() >= prefix.size()) {
            found = true;
            return false;
        }
        return true;
    });
    return !found;
}

// ==================== Index Scan ====================

void SyncGraphDataStore::scanIndexEquality(GraphSession& gs, const std::string& table, const PropertyValue& value,
                                           const std::function<bool(uint64_t)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id);
    });
}

void SyncGraphDataStore::scanIndexEquality(GraphSession& gs, const std::string& table,
                                           const std::vector<PropertyValue>& values,
                                           const std::function<bool(uint64_t)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id);
    });
}

void SyncGraphDataStore::scanIndexRange(GraphSession& gs, const std::string& table,
                                        const std::optional<PropertyValue>& start,
                                        const std::optional<PropertyValue>& end,
                                        const std::function<bool(uint64_t)>& callback) {
    std::string start_encoded;
    if (start.has_value()) {
        // Append max entity_id suffix so search_near skips past all entries
        // with the exact start value (exclusive lower bound)
        start_encoded = IndexKeyCodec::encodeSortableValue(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValue(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "scanIndexRange: cannot open cursor on " + table);
        return;
    }
    const int ret = wt::scan_range(
        cursor.get(), start_encoded, /*exclusive_start=*/true, end_encoded, stripEntityIdSuffix,
        [&](std::string_view key, std::string_view /*value*/) { return callback(IndexKeyCodec::decodeEntityId(key)); });
    if (ret != 0)
        gs.fail_from_wt(ret, "scanIndexRange failed on " + table);
}

void SyncGraphDataStore::scanIndexRange(GraphSession& gs, const std::string& table,
                                        const std::optional<std::vector<PropertyValue>>& start,
                                        const std::optional<std::vector<PropertyValue>>& end,
                                        const std::function<bool(uint64_t)>& callback) {
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValues(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValues(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "scanIndexRange: cannot open cursor on " + table);
        return;
    }
    const int ret = wt::scan_range(
        cursor.get(), start_encoded, /*exclusive_start=*/true, end_encoded, stripEntityIdSuffix,
        [&](std::string_view key, std::string_view /*value*/) { return callback(IndexKeyCodec::decodeEntityId(key)); });
    if (ret != 0)
        gs.fail_from_wt(ret, "scanIndexRange failed on " + table);
}

void SyncGraphDataStore::scanIndexEqualityWithValue(GraphSession& gs, const std::string& table,
                                                    const PropertyValue& value,
                                                    const std::function<bool(uint64_t, std::string_view)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view val) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id, val);
    });
}

void SyncGraphDataStore::scanIndexEqualityWithValue(GraphSession& gs, const std::string& table,
                                                    const std::vector<PropertyValue>& values,
                                                    const std::function<bool(uint64_t, std::string_view)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    wt::table_scan(gs, table, prefix, [&](std::string_view key, std::string_view val) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id, val);
    });
}

void SyncGraphDataStore::scanIndexRangeWithValue(GraphSession& gs, const std::string& table,
                                                 const std::optional<PropertyValue>& start,
                                                 const std::optional<PropertyValue>& end,
                                                 const std::function<bool(uint64_t, std::string_view)>& callback) {
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValue(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValue(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "scanIndexRangeWithValue: cannot open cursor on " + table);
        return;
    }
    const int ret = wt::scan_range(cursor.get(), start_encoded, /*exclusive_start=*/true, end_encoded,
                                   stripEntityIdSuffix, [&](std::string_view key, std::string_view value) {
                                       return callback(IndexKeyCodec::decodeEntityId(key), value);
                                   });
    if (ret != 0)
        gs.fail_from_wt(ret, "scanIndexRangeWithValue failed on " + table);
}

void SyncGraphDataStore::scanIndexRangeWithValue(GraphSession& gs, const std::string& table,
                                                 const std::optional<std::vector<PropertyValue>>& start,
                                                 const std::optional<std::vector<PropertyValue>>& end,
                                                 const std::function<bool(uint64_t, std::string_view)>& callback) {
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValues(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValues(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "scanIndexRangeWithValue: cannot open cursor on " + table);
        return;
    }
    const int ret = wt::scan_range(cursor.get(), start_encoded, /*exclusive_start=*/true, end_encoded,
                                   stripEntityIdSuffix, [&](std::string_view key, std::string_view value) {
                                       return callback(IndexKeyCodec::decodeEntityId(key), value);
                                   });
    if (ret != 0)
        gs.fail_from_wt(ret, "scanIndexRangeWithValue failed on " + table);
}

// ==================== Index Scan Cursor ====================

IndexScanCursorImpl::IndexScanCursorImpl(GraphSession& session, CursorHandle cursor, Spec spec)
    : session_(session), cursor_(std::move(cursor)), spec_(std::move(spec)) {
    if (!cursor_)
        return;
    position();
}

IndexScanCursorImpl::~IndexScanCursorImpl() = default;

void IndexScanCursorImpl::fail(int wt_error, const char* what) {
    valid_ = false;
    session_.fail_from_wt(wt_error, std::string("IndexScanCursor: ") + what);
}

void IndexScanCursorImpl::position() {
    auto* c = cursor_.get();
    int ret = 0;
    if (!spec_.start_key.empty()) {
        setItem(c, spec_.start_key);
        int cmp = 0;
        ret = c->search_near(c, &cmp);
        if (ret == WT_NOTFOUND)
            return; // 没有命中
        if (ret != 0) {
            fail(ret, "search_near failed");
            return;
        }
        const bool advance = spec_.prefix_mode ? (cmp < 0) : (spec_.exclusive_start ? cmp <= 0 : cmp < 0);
        if (advance) {
            ret = c->next(c);
            if (ret == WT_NOTFOUND)
                return;
            if (ret != 0) {
                fail(ret, "next failed");
                return;
            }
        }
    } else {
        ret = c->reset(c);
        if (ret != 0) {
            fail(ret, "reset failed");
            return;
        }
        ret = c->next(c);
        if (ret == WT_NOTFOUND)
            return;
        if (ret != 0) {
            fail(ret, "next failed");
            return;
        }
    }
    readCurrent();
}

void IndexScanCursorImpl::readCurrent() {
    auto* c = cursor_.get();
    std::string key;
    int ret = getKeyChecked(c, key);
    if (ret != 0) {
        fail(ret, "get_key failed");
        return;
    }
    if (spec_.prefix_mode) {
        if (!key.starts_with(spec_.start_key))
            return; // 前缀结束（正常）
    }
    if (!spec_.end_key.empty()) {
        const std::string_view sortable =
            key.size() >= 8 ? std::string_view(key).substr(0, key.size() - 8) : std::string_view(key);
        if (sortable >= spec_.end_key)
            return; // 上界结束（正常）
    }
    std::string value;
    ret = getValueChecked(c, value);
    if (ret != 0) {
        fail(ret, "get_value failed");
        return;
    }
    entity_id_ = IndexKeyCodec::decodeEntityId(key);
    value_ = std::move(value);
    valid_ = true;
}

void IndexScanCursorImpl::next() {
    if (!valid_)
        return;
    valid_ = false;
    auto* c = cursor_.get();
    const int ret = c->next(c);
    if (ret == WT_NOTFOUND)
        return;
    if (ret != 0) {
        fail(ret, "next failed");
        return;
    }
    readCurrent();
}

bool IndexScanCursorImpl::valid() const {
    return valid_;
}

uint64_t IndexScanCursorImpl::entityId() const {
    return entity_id_;
}

std::string_view IndexScanCursorImpl::value() const {
    return value_;
}

// ==================== Index Scan Cursor Factories ====================

std::unique_ptr<ISyncGraphDataStore::IIndexScanCursor>
SyncGraphDataStore::createIndexEqualityCursor(GraphSession& gs, const std::string& table, const PropertyValue& value) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createIndexEqualityCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<IndexScanCursorImpl>(gs, std::move(cursor),
                                                 IndexScanCursorImpl::Spec{prefix, {}, false, /*prefix_mode=*/true});
}

std::unique_ptr<ISyncGraphDataStore::IIndexScanCursor>
SyncGraphDataStore::createIndexEqualityCursor(GraphSession& gs, const std::string& table,
                                              const std::vector<PropertyValue>& values) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createIndexEqualityCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<IndexScanCursorImpl>(gs, std::move(cursor),
                                                 IndexScanCursorImpl::Spec{prefix, {}, false, /*prefix_mode=*/true});
}

std::unique_ptr<ISyncGraphDataStore::IIndexScanCursor>
SyncGraphDataStore::createIndexRangeCursor(GraphSession& gs, const std::string& table,
                                           const std::optional<PropertyValue>& start,
                                           const std::optional<PropertyValue>& end) {
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValue(*start);
        start_encoded.append(8, '\xFF');
    }
    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValue(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createIndexRangeCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<IndexScanCursorImpl>(
        gs, std::move(cursor),
        IndexScanCursorImpl::Spec{std::move(start_encoded), std::move(end_encoded),
                                  /*exclusive_start=*/true, /*prefix_mode=*/false});
}

std::unique_ptr<ISyncGraphDataStore::IIndexScanCursor>
SyncGraphDataStore::createIndexRangeCursor(GraphSession& gs, const std::string& table,
                                           const std::optional<std::vector<PropertyValue>>& start,
                                           const std::optional<std::vector<PropertyValue>>& end) {
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValues(*start);
        start_encoded.append(8, '\xFF');
    }
    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValues(*end);

    CursorHandle cursor = gs.acquire_scan_cursor(table);
    if (!cursor) {
        const int error = cursor.error();
        gs.fail_from_wt(error, "createIndexRangeCursor: cannot open cursor on " + table);
        return nullptr;
    }
    return std::make_unique<IndexScanCursorImpl>(
        gs, std::move(cursor),
        IndexScanCursorImpl::Spec{std::move(start_encoded), std::move(end_encoded),
                                  /*exclusive_start=*/true, /*prefix_mode=*/false});
}

} // namespace eugraph
