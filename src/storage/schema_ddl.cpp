#include "storage/schema_ddl.hpp"

#include "common/types/constants.hpp"

#include <cerrno>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== SchemaDdl ====================

bool SchemaDdl::create_tables(WtSession& session, std::initializer_list<std::string_view> tables) {
    if (tables.size() == 0)
        return true;
    if (!session) {
        spdlog::error("SchemaDdl::create_tables: null session");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mu_);

    // 只记录**本次新建**的表；EBUSY 的既有表不记录、也绝不回滚删除 ✓
    std::vector<std::string> created;
    created.reserve(tables.size());
    for (std::string_view name : tables) {
        std::string table(name);
        const int ret = session.get()->create(session.get(), table.c_str(), WT_TABLE_CONFIG);
        if (ret == 0) {
            created.push_back(std::move(table));
            continue;
        }
        if (ret == EBUSY)
            continue; // 已存在 ⇒ 视为成功 ✓

        spdlog::error("SchemaDdl::create_tables: create {} failed: {}", name, wiredtiger_strerror(ret));
        // 逆序回滚本次新建的表；失败只记日志（尽力而为，但绝不删既有表 ✓）
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            const int drop_ret = session.get()->drop(session.get(), it->c_str(), nullptr);
            if (drop_ret != 0)
                spdlog::error("SchemaDdl::create_tables: rollback drop {} failed: {}", *it,
                              wiredtiger_strerror(drop_ret));
        }
        return false;
    }
    return true;
}

bool SchemaDdl::drop_tables(WtSession& session, std::initializer_list<std::string_view> tables, DropMode mode) {
    if (tables.size() == 0)
        return true;
    if (!session) {
        spdlog::error("SchemaDdl::drop_tables: null session");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (mode == DropMode::CheckpointFirst) {
        const int ret = session.get()->checkpoint(session.get(), nullptr);
        if (ret != 0)
            spdlog::warn("SchemaDdl::drop_tables: checkpoint before drop failed (continuing)");
    }

    bool ok = true;
    for (std::string_view name : tables) {
        std::string table(name);
        const int ret = session.get()->drop(session.get(), table.c_str(), nullptr);
        if (ret != 0 && ret != ENOENT) {
            spdlog::error("SchemaDdl::drop_tables: drop {} failed: {}", name, wiredtiger_strerror(ret));
            ok = false; // 继续尝试其余表：尽力删除、让重试能收敛 ✓
        }
    }
    return ok;
}

bool SchemaDdl::ensure_tables(WtSession& session, std::initializer_list<std::string_view> tables) {
    if (tables.size() == 0)
        return true;
    if (!session) {
        spdlog::error("SchemaDdl::ensure_tables: null session");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mu_);

    for (std::string_view name : tables) {
        std::string table(name);
        const int ret = session.get()->create(session.get(), table.c_str(), WT_TABLE_CONFIG);
        if (ret != 0 && ret != EBUSY) {
            spdlog::error("SchemaDdl::ensure_tables: create {} failed: {}", name, wiredtiger_strerror(ret));
            return false;
        }
    }
    return true;
}

} // namespace eugraph
