#pragma once
/// 一次原子读取的 schema 版本：定义、快照代数、兼容版本必须来自同一发布点。
///
/// 现有 `schemaPtr()` / `schemaGeneration()` 分开读会在 DDL 发布窗口内交错：
/// 事务可能拿到"新代数 + 旧 schema"（或反之），从而过早看到索引/标签。
/// 需要在事务 BEGIN/批量事务开始时成对读取的地方统一用本结构。

#include "common/schema/graph_schema.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace eugraph {

struct SchemaVersion {
    std::shared_ptr<const GraphSchema> schema;
    uint64_t generation = 0;
    uint64_t compat_version = 0;

    explicit operator bool() const noexcept {
        return schema != nullptr;
    }
};

} // namespace eugraph
