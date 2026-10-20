#pragma once

// `SchemaDdl`：单连接内 **DDL 的唯一入口** ✓
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3。
//  - 接收调用方传入的 `WtSession&`（**非事务**会话）✓
//  - 内部持有 schema 序列锁（只保护 DDL/lifecycle 序列，不是每 KV 调用的锁 ✓）
//      * `create_tables`：全成功 ⇒ true；任一失败 ⇒ **只回滚本次新建**的表
//        （`EBUSY` 的既有表绝不能删 ✗），再返回 false ✓
//      * `drop_tables`：全部尝试；`ENOENT` 视为成功 ⇒ 幂等、可重试收敛 ✓
//      * `ensure_tables`：`EBUSY` 视为成功 ✓
//
// 边界：**跨连接无原子性** ✗（data / meta 是两个 WT 连接、各自一个 `SchemaDdl`）。
// 保持“元数据先写、数据表后建”，由 CREATE 路径的幂等 ensure 兜底 ✓。

#include "storage/wt_session.hpp"

#include <initializer_list>
#include <mutex>
#include <string_view>

namespace eugraph {

/// 单连接内 DDL 的唯一入口（无状态依赖，只依赖调用方传入的 session ✓）
class SchemaDdl {
public:
    SchemaDdl() = default;
    SchemaDdl(const SchemaDdl&) = delete;
    SchemaDdl& operator=(const SchemaDdl&) = delete;

    /// 删表前的 checkpoint 策略：保持原有 label/edge label（需要）与 index（不需要）的差异 ✓
    enum class DropMode {
        Plain,
        CheckpointFirst
    };

    /// 建一组表：要么全部成功，要么回滚**本次新建**的表并返回 false ✓
    bool create_tables(WtSession& session, std::initializer_list<std::string_view> tables);
    /// 尽力删除全部；`ENOENT` 视为成功 ⇒ 幂等 ✓；返回是否没有真实错误
    bool drop_tables(WtSession& session, std::initializer_list<std::string_view> tables,
                     DropMode mode = DropMode::Plain);
    /// 确保全部存在；`EBUSY`（已存在）视为成功 ✓
    bool ensure_tables(WtSession& session, std::initializer_list<std::string_view> tables);

private:
    std::recursive_mutex mu_;
};

} // namespace eugraph
