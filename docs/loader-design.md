# CSV Loader 设计文档

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

## 1. 概述

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 LDBC SNB 格式的 CSV 数据导入图数据库。

## 2. 数据格式分析

### 2.1 目录结构

```
social_network-sf0.1-CsvComposite-LongDateFormatter/
├── static/          # 静态数据（organisation, place, tag, tagclass）
└── dynamic/         # 动态数据（comment, forum, person, post）
```

### 2.2 文件命名规则

- **点文件**: `{label}_0_0.csv`，例: `person_0_0.csv`
- **边文件**: `{srcLabel}_{edgeType}_{dstLabel}_0_0.csv`，例: `person_knows_person_0_0.csv`

分类方法：文件名按 `_` 分割后：
- 3 段 → 点文件（label, 0, 0）
- 5 段 → 边文件（srcLabel, edgeType, dstLabel, 0, 0）

### 2.3 CSV 格式

- 分隔符: `|`（pipe）
- 首行为表头

**点文件格式**:
```
id|prop1|prop2|...
933|value1|value2|...
```
- 首列 `id` 为 CSV 主键（INT64）
- 后续列为属性

**边文件格式**:
```
SrcLabel.id|DstLabel.id|prop1|...
933|1129|value1|...
```
- 前两列为起点和终点的 CSV 主键
- 后续列为边属性（可能没有属性列）

### 2.4 ID 特征

CSV 中的 `id` 值**跨标签不唯一**（place、organisation、tag 均从 0 开始），因此不能直接用作 eugraph 全局主键。需要建立 (label, csv_id) → VertexId 的映射关系。

## 3. 装载流程

```
┌─────────────────────────────────────────┐
│  1. 扫描目录，收集并分类 CSV 文件         │
│  2. 解析表头，推断属性类型                │
│  3. 创建所有标签（DDL）                  │
│  4. 创建所有关系类型（DDL）               │
│  5. 逐文件写入点数据 → 收集 ID 映射       │
│  6. 逐文件写入边数据（使用映射解析顶点）   │
└─────────────────────────────────────────┘
```

### 3.1 阶段一：扫描与分类

递归扫描 `data-dir` 下所有 `.csv` 文件，根据文件名段数分类：

```cpp
struct CsvFileInfo {
    std::filesystem::path path;
    bool is_vertex;           // true=点文件, false=边文件
    std::string label;        // 点: 标签名; 边: 同 (仅点文件用)
    std::string src_label;    // 边文件: 起点标签名
    std::string edge_type;    // 边文件: 关系类型名
    std::string dst_label;    // 边文件: 终点标签名
};
```

### 3.2 阶段二：Schema 创建

从所有点文件中提取标签名和属性定义，从所有边文件中提取关系类型名和属性定义。

**点标签 Schema**:
- 读取点文件首行（表头），首列 `id` 为 CSV 主键（不作为属性）
- 剩余列作为属性定义

**关系类型 Schema**:
- 读取边文件首行（表头），前两列为 src/dst 主键引用（不作为边属性）
- 剩余列作为边属性定义
- 同一关系类型可能出现在多个文件中（如 `person_likes_comment` 和 `person_likes_post` 都含 `likes`），需合并属性定义

**属性类型推断**（采样前 N 行）:
1. 尝试解析为 `int64_t` → INT64
2. 否则 → STRING

当前 LDBC SNB 数据集仅需 INT64 和 STRING 两种类型。

### 3.3 阶段三：写入点数据

对每个点文件：
1. 批量读取 CSV 行（默认 batch_size = 1000）
2. 调用 `batchInsertVertices` RPC
3. 将返回的 VertexId 存入映射表: `(label, csv_id) → VertexId`

### 3.4 阶段四：写入边数据

对每个边文件：
1. 批量读取 CSV 行
2. 通过映射表查找 src/dst 的 VertexId
3. 调用 `batchInsertEdges` RPC

## 4. 服务端扩展

需要新增两个批量写入 RPC 方法。

### 4.1 新增 Thrift 类型

```thrift
struct VertexRecord {
  1: PropertyValueThrift pk_value           // CSV id，作为主键存储
  2: list<PropertyValueThrift> properties   // 属性值，按属性定义顺序
}

struct EdgeRecord {
  1: i64 src_vertex_id                      // 起点内部 VertexId（客户端已解析）
  2: i64 dst_vertex_id                      // 终点内部 VertexId（客户端已解析）
  3: list<PropertyValueThrift> properties   // 属性值，按属性定义顺序
}

struct BatchInsertVerticesResult {
  1: list<i64> vertex_ids                   // 服务器分配的 VertexId 列表，与输入顺序一致
  2: i32 count                              // 成功插入数量
}
```

### 4.2 新增 RPC 方法

```thrift
service EuGraphService {
  // ... 已有方法 ...

  BatchInsertVerticesResult batchInsertVertices(
    1: string label_name,
    2: list<VertexRecord> records
  )

  i32 batchInsertEdges(
    1: string edge_label_name,
    2: list<EdgeRecord> records
  )
}
```

**`batchInsertVertices` 流程**:
1. 通过 `label_name` 查找 `LabelId`
2. 为每条记录通过 `nextVertexId()` 分配 VertexId
3. 调用 `insertVertex` 写入顶点（设置 pk_value 为 CSV id）
4. 返回所有分配的 VertexId

**`batchInsertEdges` 流程**:
1. 通过 `edge_label_name` 查找 `EdgeLabelId`
2. 为每条记录通过 `nextEdgeId()` 分配 EdgeId
3. 调用 `insertEdge` 写入边（使用客户端提供的 src/dst VertexId）
4. 返回插入数量

### 4.3 Handler 实现

`batchInsertVertices` 的事务策略：整个批次在同一个事务中执行，失败则回滚整个批次。

## 5. 客户端工具设计

### 5.1 程序入口

```
eugraph-loader --host <host> --port <port> --data-dir <path> [--batch-size <N>]
```

参数：
- `--host`: server 地址，默认 `127.0.0.1`
- `--port`: server 端口，默认 `9090`
- `--data-dir`: 数据目录路径（包含 static/ 和 dynamic/ 子目录）
- `--batch-size`: 每批 RPC 请求的记录数，默认 `1000`

### 5.2 模块结构

```
src/loader/
├── loader_main.cpp          # 入口：参数解析、流程编排
├── csv_scanner.hpp/cpp      # 目录扫描、文件分类
├── csv_parser.hpp/cpp       # CSV 文件解析（pipe 分隔）
├── schema_builder.hpp/cpp   # 从 CSV 表头构建标签/关系类型定义
└── data_loader.hpp/cpp      # 批量数据写入逻辑
```

### 5.3 关键数据结构

```cpp
// CSV 主键 → 内部 VertexId 映射
using CsvIdMap = std::unordered_map<std::string, std::unordered_map<int64_t, VertexId>>;
// label_name → { csv_id → vertex_id }
```

写入点数据时填充 `CsvIdMap`，写入边数据时从中查找 src/dst VertexId。

### 5.4 CSV 解析

- 使用 `std::getline` 逐行读取
- 按 `|` 分割字段
- 首行解析为表头
- 空字段表示属性值为 null（`std::monostate`）

## 6. 错误处理

| 场景 | 处理方式 |
|------|---------|
| server 连接失败 | 报错退出 |
| CSV 文件解析失败 | 报错退出，打印文件名和行号 |
| 标签/关系类型已存在 | 跳过创建，继续装载 |
| 批量写入失败 | 回滚当前批次，报错退出 |
| 边的 src/dst 顶点未找到 | 报错退出，打印缺失的顶点信息 |

## 7. 构建集成

在顶层 `CMakeLists.txt` 中新增 `eugraph-loader` 可执行目标，复用现有的 fbthrift 依赖和 `EuGraphRpcClient` 代码。
