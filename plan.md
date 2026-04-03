# EuGraph 开发计划

## 项目目标

实现一个分布式图数据库，支持 GSQL 查询语言。

---

## 当前阶段：阶段一 基础框架 (MVP) — 进行中

**设计文档**：参见 [docs/overview.md](docs/overview.md)

| 主题 | 文档 |
|------|------|
| 系统架构 | [docs/system-architecture.md](docs/system-architecture.md) |
| 数据模型 | [docs/data-model.md](docs/data-model.md) |
| KV 编码 | [docs/kv-encoding.md](docs/kv-encoding.md) |
| 类型定义 | [docs/type-definitions.md](docs/type-definitions.md) |
| 接口设计 | [docs/interfaces.md](docs/interfaces.md) |
| 目录结构 | [docs/directory-structure.md](docs/directory-structure.md) |
| 开发阶段 | [docs/development-phases.md](docs/development-phases.md) |

### 任务进度

| # | 任务 | 状态 |
|---|------|------|
| 1.1 | 项目骨架搭建（CMake、目录结构、基础类型） | ✅ 已完成 |
| 1.2 | KV 存储引擎集成（IKVEngine、RocksDBStore、KeyCodec、ValueCodec） | ✅ 已完成 |
| 1.3 | 图存储层（IGraphStore、GraphStoreImpl、Vertex/Edge CRUD） | ✅ 已完成 |
| 1.4 | 元数据服务（Label/EdgeLabel 管理、ID 分配） | 待开始 |
| 1.5 | 基础事务支持（MVCC） | 待开始 |

### 已完成的工作

#### 1.1 项目骨架搭建 ✅
- CMakeLists.txt 配置（vcpkg 依赖管理：rocksdb, gtest, spdlog, lz4, zlib, zstd）
- src/ 目录结构创建
- 基础类型定义（graph_types.hpp, error.hpp, constants.hpp）

#### 1.2 KV 存储引擎集成 ✅
- IKVEngine 抽象接口（put/get/del、prefix scan、事务）
- RocksDBStore 实现（基于 RocksDB 的 IKVEngine 实现）
- KeyCodec（各类型 Key 的编解码：L/I/P/R/X/E/D 前缀）
- ValueCodec（PropertyValue 的序列化/反序列化，支持所有类型及压缩）
- 单元测试（test_key_codec.cpp, test_value_codec.cpp）

#### 1.3 图存储层 ✅
- IGraphStore 抽象接口（Vertex/Edge CRUD、标签管理、边遍历、主键查询、统计）
- GraphStoreImpl 实现（基于 IKVEngine 的图语义存储）
- 支持功能：
  - 顶点增删、属性读写（单个/批量）、标签增删
  - 边增删、属性读写、双向索引
  - 主键正反向查询
  - 按标签扫描顶点、边遍历
  - 事务支持（提交/回滚）
- 单元测试（test_graph_store.cpp）
- **设计决策**：`insertVertex` 允许不指定标签（后续可通过 `addVertexLabel` + `putVertexProperties` 补充）

### 构建方式
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/mnt/f/code/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/eugraph_tests
```

---

## 后续阶段（待规划）

### 阶段三：查询能力
- GSQL 解析器
- 查询规划与执行

### 阶段四：分布式扩展
- RPC 通信（fbthrift）
- 分布式事务（2PC）

---

## 设计调整记录

此处记录对设计文档的修改和调整决策。

| 日期 | 修改内容 | 原因 |
|------|----------|------|
| 2025-03-31 | 属性存储（X\|）Key 改用 prop_id 代替 property_name | 支持部分属性查询，字段改名无需修改数据存储 |
| 2025-03-31 | Edge 数据（D\|）改为 D\|{edge_label_id}\|{edge_id}\|{prop_id}，属性分开存储 | 支持部分属性查询，支持遍历某关系类型下所有边 |
| 2025-03-31 | 元数据 properties 改为数组，包含 prop_id 映射 | 支持属性名与 prop_id 的双向查找 |
| 2025-03-31 | 内存结构 Properties 改为按 prop_id 索引的数组 | 与 KV 存储设计一致，内存更紧凑，访问更快 |
| 2025-03-31 | Edge 数据（D\|）改为 D\|{edge_label_id}\|{edge_id}\|{prop_id}，属性分开存储 | 与 Vertex 属性存储（X\|）设计一致，支持部分属性查询 |
| 2025-03-31 | 主键明确定义为全局唯一标识，每顶点一个 | 主键是顶点的全局标识，不属于任何标签 |
| 2025-03-31 | 新增主键反向索引 R\|{vertex_id} → {pk_value} | 支持从 vertex_id 反查全局主键 |
| 2025-03-31 | 移除 LabelDef 中的 primary_key_prop_id 字段 | 标签内唯一约束由 IndexDef(unique=true) 表达 |
| 2026-04-01 | KV 存储引擎从 WiredTiger 切换为 RocksDB | WiredTiger 在 GCC 15 下存在 DT_INIT 链接问题，RocksDB 更成熟且 vcpkg 支持更好 |
| 2026-04-01 | 引入 IKVEngine 抽象接口 + IGraphStore 图语义层 | 解耦存储引擎与图操作，支持后续替换底层引擎 |
| 2026-04-01 | 新增 putVertexProperties 批量属性设置接口 | 一次写入某标签下所有属性，减少多次单独调用 |
| 2026-04-01 | insertVertex 允许不指定标签 | 支持先创建顶点再逐步补充标签和属性的场景 |

---

## 待开发需求列表

以下需求按优先级排列，逐个对齐设计与实现方案后开始开发。

### A. MVCC（多版本并发控制）

| # | 需求 | 状态 |
|---|------|------|
| A.1 | 为数据加上版本，查询时获取当前事务可见的最新版本 | 待对齐 |
| A.2 | 保证写入多条数据的原子性 | 待对齐 |

### B. DDL（数据定义语言）

| # | 需求 | 说明 | 状态 |
|---|------|------|------|
| B.1 | 删除图 | 要求速度快，[设计文档](docs/multi-graph.md) | 设计完成 |
| B.2 | 删除某个标签类型 | 要求速度快，[设计文档](docs/ddl.md#一删除标签类型drop-label) | 设计完成 |
| B.3 | 删除某个关系类型 | 需同时删除该类型的所有边，[设计文档](docs/ddl.md#二删除关系类型drop-edgelabel) | 设计完成 |
| B.4 | 删除某个标签下的某个列 | [设计文档](docs/ddl.md#三列操作column-ddl) | 设计完成 |
| B.5 | 删除某个关系类型下的某个列 | 同 B.4 | 设计完成 |
| B.6 | 重命名某个标签类型下的某个列 | 纯元数据操作，见 DDL 文档 | 设计完成 |
| B.7 | 重命名关系类型下的某个列 | 纯元数据操作，见 DDL 文档 | 设计完成 |
| B.8 | 添加列 | 需指定默认值或允许为空，避免回填数据，见 DDL 文档 | 设计完成 |

---

## 待讨论问题

此处记录需要进一步讨论或确认的设计问题。

1. （待补充）
