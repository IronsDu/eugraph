#pragma once

// WT **表级原语**（自由函数，无状态 ✓）
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3。
// 职责**仅限**：
//  - 表级 KV：显式 WT session（admin/meta）与 `GraphSession`（用户事务）两条路径 ✓
//  - 区间/前缀迭代的**唯一原语** `scan_range()` ✓（table scan 与 index range 共用）
// **不做** ✗：会话创建（`WtConnectionStore::make_session()` ✓）、DDL 序列（`SchemaDdl` ✓）、
// 事务（`GraphSession` ✓）、业务语义（各 store ✓）。
//
// 会话由调用方创建并传入：不再有 `WtTableOps` 对象 / `open_admin_session` 概念 ✓。

#include "storage/wt_cursor.hpp"
#include "storage/wt_session.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace eugraph {

class GraphSession;

namespace wt {

/// 开一个**独立** cursor（扫描用；不复用会话缓存，避免与外层扫描互相踩 ✓）
[[nodiscard]] inline WtCursor open_cursor(WT_SESSION* session, const std::string& table_name) {
    return WtCursor(session, table_name);
}

// ==================== 显式 WT session 路径（admin / meta）====================
bool table_put(WT_SESSION* session, const std::string& table, std::string_view key, std::string_view value);
[[nodiscard]] std::optional<std::string> table_get(WT_SESSION* session, const std::string& table, std::string_view key);
bool table_del(WT_SESSION* session, const std::string& table, std::string_view key);
void table_scan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                const std::function<bool(std::string_view, std::string_view)>& callback);

// ==================== GraphSession 路径（用户事务）========================
/// point 操作用**会话级 cursor 缓存** ✓（§7：无 cursor 池，cursor 按会话缓存 ✓）
bool table_put(GraphSession& session, const std::string& table, std::string_view key, std::string_view value);
[[nodiscard]] std::optional<std::string> table_get(GraphSession& session, const std::string& table,
                                                   std::string_view key);
bool table_del(GraphSession& session, const std::string& table, std::string_view key);
/// 扫描用独立 cursor（同表嵌套扫描/边扫边删是真实路径 ✓，共享缓存 cursor 会互相破坏 ✗）
void table_scan(GraphSession& session, const std::string& table, std::string_view prefix,
                const std::function<bool(std::string_view, std::string_view)>& callback);

/// 区间/前缀迭代的**统一原语** ✓（tableScan 与 index range scan 共用）
///  - start_key 空 ⇒ 从头开始；exclusive_start ⇒ 跳过与 start_key 相等的键 ✓
///  - end_key_view 把完整 key 转成与 end_key 可比的视图（索引区间需剥掉 entity_id 后缀）✓
/// 返回 0 = 正常结束；非 0 = WT 错误码（会话路径必须 `session.fail_from_wt()` ✓）。
int scan_range(WT_CURSOR* cursor, std::string_view start_key, bool exclusive_start, std::string_view end_key,
               const std::function<std::string_view(std::string_view)>& end_key_view,
               const std::function<bool(std::string_view, std::string_view)>& callback);

} // namespace wt

} // namespace eugraph
