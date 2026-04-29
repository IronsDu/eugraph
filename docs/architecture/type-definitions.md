# 核心类型定义

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

源码：`src/common/types/graph_types.hpp`

---

## ID 类型

```cpp
using VertexId = uint64_t;
using EdgeId = uint64_t;
using LabelId = uint16_t;
using EdgeLabelId = uint16_t;
using LabelName = std::string;
using EdgeLabelName = std::string;
using GraphTxnHandle = void*;  // 不透明事务句柄，指向内部 TxnState

constexpr VertexId INVALID_VERTEX_ID = 0;
constexpr EdgeId INVALID_EDGE_ID = 0;
constexpr LabelId INVALID_LABEL_ID = 0;
constexpr EdgeLabelId INVALID_EDGE_LABEL_ID = 0;
constexpr GraphTxnHandle INVALID_GRAPH_TXN = nullptr;
```

---

## 属性值

```cpp
using PropertyValue = variant<monostate, bool, int64_t, double, string,
                              vector<int64_t>, vector<double>, vector<string>>;
using Properties = vector<optional<PropertyValue>>;  // 按 prop_id 索引的稀疏数组

enum class PropertyType { BOOL, INT64, DOUBLE, STRING, INT64_ARRAY, DOUBLE_ARRAY, STRING_ARRAY };

struct PropertyDef {
    uint16_t id = 0;
    string name;
    PropertyType type;
    bool required = false;
    optional<PropertyValue> default_value;
};
```

Properties 向量以 prop_id 为下标，prop_id 从 1 开始（索引 0 不使用）。prop_id 在每个 Label/EdgeLabel 内独立编号、永不复用。

---

## 顶点与边

```cpp
struct Vertex {
    VertexId id;
    Properties properties;  // 注意：不含 labels（labels 通过 L| 索引单独查询）
};

struct Edge {
    EdgeId id;
    VertexId src_id;
    VertexId dst_id;
    EdgeLabelId label_id;
    Direction direction;     // 仅内存对象，不持久化
    Properties properties;
};

enum class Direction : uint8_t { OUT = 0x00, IN = 0x01, BOTH = 0x02 };
using LabelIdSet = set<LabelId>;
```

---

## 索引

```cpp
enum class IndexState : uint8_t {
    WRITE_ONLY  = 0,  // 创建中：写入维护索引，查询不可用
    PUBLIC      = 1,  // 已就绪：完全可用
    DELETE_ONLY = 2,  // 删除中：仅清理残留条目
    ERROR       = 3,  // 异常：唯一索引回填发现重复值
};
```

---

## Label / EdgeLabel 定义

```cpp
struct LabelDef {
    LabelId id = INVALID_LABEL_ID;
    LabelName name;
    vector<PropertyDef> properties;

    struct IndexDef {
        string name;
        vector<uint16_t> prop_ids;
        bool unique = false;
        IndexState state = IndexState::WRITE_ONLY;
    };
    vector<IndexDef> indexes;
};

struct EdgeLabelDef {
    EdgeLabelId id = INVALID_EDGE_LABEL_ID;
    EdgeLabelName name;
    vector<PropertyDef> properties;
    bool directed = true;
    vector<LabelDef::IndexDef> indexes;  // 边属性索引
};
```

---

## 运行时类型

```cpp
// 查询执行中的值类型（src/compute_service/executor/row.hpp）
struct VertexValue { VertexId id; LabelIdSet labels; Properties properties; };
struct EdgeValue { EdgeId id; VertexId src_id, dst_id; EdgeLabelId label_id; Properties properties; };
using ListValue = vector<Value>;
using Value = variant<monostate, bool, int64_t, double, string, VertexValue, EdgeValue, ListValue>;

using Row = vector<Value>;         // 位置式行
using Schema = vector<string>;     // 列名列表

struct RowBatch {
    static constexpr size_t CAPACITY = 1024;
    vector<Row> rows;
};
```
