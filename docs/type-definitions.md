# 核心类型定义

> 参见 [overview.md](overview.md) 返回文档导航

```cpp
// src/common/types/graph_types.hpp

#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <variant>
#include <unordered_map>
#include <set>
#include <vector>

namespace eugraph {

// ==================== ID 类型 ====================
using VertexId = uint64_t;
using EdgeId = uint64_t;
using TransactionId = uint64_t;
using SnapshotId = uint64_t;
using SessionId = uint64_t;

using LabelId = uint16_t;
using EdgeLabelId = uint16_t;

using LabelName = std::string;
using EdgeLabelName = std::string;

// 特殊值
constexpr VertexId INVALID_VERTEX_ID = 0;
constexpr EdgeId INVALID_EDGE_ID = 0;
constexpr LabelId INVALID_LABEL_ID = 0;
constexpr EdgeLabelId INVALID_EDGE_LABEL_ID = 0;

// ==================== Label ID 集合 ====================
using LabelIdSet = std::set<LabelId>;

// ==================== 属性值类型 ====================
using PropertyValue = std::variant<
    std::monostate,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>
>;

// 属性值按 prop_id 索引存储，prop_id 作为数组下标
// 示例：properties[1] 存储 prop_id=1 的属性值
using Properties = std::vector<std::optional<PropertyValue>>;

// ==================== 顶点 ====================
struct Vertex {
    VertexId id;
    Properties properties;
};

// ==================== 边 ====================
struct Edge {
    EdgeId id;
    VertexId src_id;
    VertexId dst_id;
    EdgeLabelId label_id;
    Direction direction;    // 仅内存对象，不持久化（从索引 key 解析）
    Properties properties;
};

// ==================== 方向 ====================
enum class Direction : uint8_t {
    OUT = 0x00,
    IN  = 0x01,
    BOTH = 0x02
};

// ==================== 隔离级别 ====================
enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

// ==================== 属性类型 ====================
enum class PropertyType {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    INT64_ARRAY,
    DOUBLE_ARRAY,
    STRING_ARRAY
};

struct PropertyDef {
    uint16_t id = 0;                    // 属性 ID（在 Label/EdgeLabel 内独立编号）
    std::string name;
    PropertyType type;
    bool required = false;
    std::optional<PropertyValue> default_value;
};

// ==================== 标签定义 ====================
struct LabelDef {
    LabelId id = INVALID_LABEL_ID;
    LabelName name;
    std::vector<PropertyDef> properties;

    struct IndexDef {
        std::string name;
        std::vector<uint16_t> prop_ids;  // 索引包含的属性 ID 列表
        bool unique = false;
    };
    std::vector<IndexDef> indexes;
};

// ==================== 边标签定义 ====================
struct EdgeLabelDef {
    EdgeLabelId id = INVALID_EDGE_LABEL_ID;
    EdgeLabelName name;
    std::vector<PropertyDef> properties;
    bool directed = true;
};

} // namespace eugraph
```
