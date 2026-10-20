#pragma once

#include "common/types/graph_types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace eugraph {

/// Synchronous graph metadata storage interface.
///
/// 连接由 `WtConnectionStore` 拥有 ✓；本接口只做元数据 KV 与管理类操作 ✓。
/// 元数据是**管理类**访问（schema 变更 / 启动扫描 / ID 高水位持久化），
/// 每次操作用单次 admin session ✓（与用户事务严格分离 ✓）。
class ISyncGraphMetaStore {
public:
    virtual ~ISyncGraphMetaStore() = default;

    // ==================== Metadata Raw KV ====================

    virtual bool metadataPut(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> metadataGet(std::string_view key) = 0;
    virtual bool metadataDel(std::string_view key) = 0;
    virtual void metadataScan(std::string_view prefix,
                              const std::function<bool(std::string_view, std::string_view)>& callback) = 0;
};

} // namespace eugraph
