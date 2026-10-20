#pragma once

#include "common/types/constants.hpp"
#include "storage/meta/i_sync_graph_meta_store.hpp"
#include "storage/schema_ddl.hpp"
#include "storage/wt_connection_store.hpp"

#include <cassert>
#include <mutex>
#include <string>
#include <unordered_map>

#include <wiredtiger.h>

namespace eugraph {

/// ISyncGraphMetaStore implementation using WiredTiger.
///
/// 连接由 `WtConnectionStore` 拥有 ✓（本类只 bind ✓）；无事务表 ✗。
class SyncGraphMetaStore : public ISyncGraphMetaStore {
public:
    SyncGraphMetaStore() = default;
    ~SyncGraphMetaStore() override = default;

    SyncGraphMetaStore(const SyncGraphMetaStore&) = delete;
    SyncGraphMetaStore& operator=(const SyncGraphMetaStore&) = delete;

    /// 初始化期绑定连接（连接归 WtConnectionStore ✓）
    void bind_connection(WtConnectionStore& conn) noexcept {
        conn_ = &conn;
    }
    [[nodiscard]] bool isOpen() const noexcept {
        return conn_ != nullptr && conn_->is_open();
    }
    [[nodiscard]] WtConnectionStore& conn() noexcept {
        assert(conn_ != nullptr);
        return *conn_;
    }

    /// 建表（幂等 ✓）：GraphManager 在 bind 之后调用 ✓（内部创建局部 WtSession）
    bool ensure_tables();

    // Metadata Raw KV
    bool metadataPut(std::string_view key, std::string_view value) override;
    std::optional<std::string> metadataGet(std::string_view key) override;
    bool metadataDel(std::string_view key) override;
    void metadataScan(std::string_view prefix,
                      const std::function<bool(std::string_view, std::string_view)>& callback) override;

private:
    WtConnectionStore* conn_ = nullptr;
    SchemaDdl schema_ddl_;
};

} // namespace eugraph
