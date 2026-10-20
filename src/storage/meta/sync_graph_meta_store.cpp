#include "storage/meta/sync_graph_meta_store.hpp"

#include "storage/wt_table_ops.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== DDL ====================

bool SyncGraphMetaStore::ensure_tables() {
    if (!isOpen()) {
        spdlog::error("SyncGraphMetaStore::ensure_tables: connection not bound");
        return false;
    }
    WtSession session = conn().make_session();
    return schema_ddl_.ensure_tables(session, {TABLE_METADATA});
}

// ==================== Metadata Raw KV ====================

bool SyncGraphMetaStore::metadataPut(std::string_view key, std::string_view value) {
    WtSession session = conn().make_session();
    if (!session)
        return false;
    return wt::table_put(session.get(), TABLE_METADATA, key, value);
}

std::optional<std::string> SyncGraphMetaStore::metadataGet(std::string_view key) {
    WtSession session = conn().make_session();
    if (!session)
        return std::nullopt;
    return wt::table_get(session.get(), TABLE_METADATA, key);
}

bool SyncGraphMetaStore::metadataDel(std::string_view key) {
    WtSession session = conn().make_session();
    if (!session)
        return false;
    return wt::table_del(session.get(), TABLE_METADATA, key);
}

void SyncGraphMetaStore::metadataScan(std::string_view prefix,
                                      const std::function<bool(std::string_view, std::string_view)>& callback) {
    WtSession session = conn().make_session();
    if (!session)
        return;
    wt::table_scan(session.get(), TABLE_METADATA, prefix, callback);
}

} // namespace eugraph
