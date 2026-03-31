# EuGraph 开发计划

## 项目目标

实现一个分布式图数据库，支持 GSQL 查询语言。

---

## 当前阶段：阶段一 基础框架 (MVP) — 进行中

**详细设计文档**：[architecture.md](./architecture.md)

### 任务进度

| # | 任务 | 状态 |
|---|------|------|
| 1.1 | 项目骨架搭建（CMake、目录结构、基础类型） | ✅ 已完成 |
| 1.2 | WiredTiger 集成（WiredTigerStore、KeyCodec、ValueCodec） | ✅ 编码完成，编译通过，运行时段错误待排查 |
| 1.3 | 元数据服务（Label/EdgeLabel 管理、ID 分配） | 待开始 |
| 1.4 | 图存储服务（Vertex/Edge CRUD） | 待开始 |
| 1.5 | 基础事务支持（MVCC） | 待开始 |

### 已完成的工作

#### 1.1 项目骨架搭建 ✅
- CMakeLists.txt 配置（vcpkg 依赖管理：wiredtiger, gtest, spdlog, lz4, zlib, zstd）
- src/ 目录结构创建
- 基础类型定义（graph_types.hpp, error.hpp, constants.hpp）

#### 1.2 WiredTiger 集成 ✅（编码完成）
- WiredTigerStore 类（连接管理、put/get/del、cursor scan、事务）
- KeyCodec（各类型 Key 的编解码：L/I/P/R/X/E/D 前缀）
- ValueCodec（PropertyValue 的序列化/反序列化，支持所有类型及压缩）
- 单元测试（test_key_codec.cpp, test_value_codec.cpp, test_wiredtiger_store.cpp）
- **已知问题**：测试运行时段错误（segfault），待排查

### 构建方式
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/home/dodo/code/vcpkg/scripts/buildsystems/vcpkg.cmake
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

此处记录对 architecture.md 的修改和调整决策。

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

---

## 待讨论问题

此处记录需要进一步讨论或确认的设计问题。

1. （待补充）
