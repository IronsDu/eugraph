#pragma once
/// 索引维护条目的**唯一定义**（存储层与 compute 层共用，避免两份结构漂移）。
///
/// 字段：
///   * `table`     —— 索引表名（仅当 registry 不可用时作为 legacy 直写目标）；
///   * `values`    —— 索引键的列值（复合索引按声明顺序）；
///   * `vid/eid`   —— 实体 id；
///   * `unique`    —— 是否唯一索引（决定 PUBLIC 时是否查重）；
///   * `index_id`  —— v2 运行时路由键（0 ⇒ legacy 直写）。
///
/// 本文件**不依赖** query 层；compute 层的 `VertexIndexEntry`/`EdgeIndexEntry`
/// 只是这里的别名（`using`），字段保持一致。

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {

struct VertexIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    VertexId vid = INVALID_VERTEX_ID;
    bool unique = false;
    uint32_t index_id = 0;
};

struct EdgeIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    EdgeId eid = INVALID_EDGE_ID;
    bool unique = false;
    uint32_t index_id = 0;
};

} // namespace eugraph
