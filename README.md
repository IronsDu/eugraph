# EuGraph

分布式图数据库，支持 GSQL 查询语言。

## 核心特性

| 特性 | 说明 |
|------|------|
| 查询语言 | GSQL（类 SQL 的图查询语言） |
| 事务模型 | MVCC + 2PC（分布式） |
| 隔离级别 | 可重复读（REPEATABLE_READ） |
| 部署模式 | 单机 → 分布式 |
| 架构模式 | 模块化、服务化 |

## 技术栈

| 组件 | 选择 | 说明 |
|------|------|------|
| 语言 | C++20 | |
| 构建系统 | CMake | vcpkg 依赖管理 |
| 协程库 | folly | Meta 成熟协程库 |
| RPC | fbthrift | folly 原生支持 |
| KV 存储 | RocksDB | 高性能、支持事务 |
| 日志 | spdlog | 高性能日志 |
| 测试 | GoogleTest | |

## 项目状态

当前处于 **阶段一：基础框架 (MVP)**。

| 模块 | 状态 |
|------|------|
| 项目骨架搭建 | 已完成 |
| KV 存储引擎（RocksDB） | 已完成 |
| 图存储层（Vertex/Edge CRUD） | 已完成 |
| 元数据服务 | 待开始 |
| 基础事务支持（MVCC） | 待开始 |

## 构建

### 前置条件

- C++20 编译器（GCC 12+ / Clang 15+）
- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg)

### 编译

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 运行测试

```bash
./build/eugraph_tests
```

## 文档

设计文档位于 [docs/](docs/) 目录，入口见 [docs/overview.md](docs/overview.md)。

| 文档 | 说明 |
|------|------|
| [系统架构](docs/system-architecture.md) | 服务定义、组合模式、架构图 |
| [数据模型](docs/data-model.md) | Vertex/Edge 概念、Label ID 映射 |
| [KV 编码](docs/kv-encoding.md) | Key 前缀、编码规则、查询能力 |
| [类型定义](docs/type-definitions.md) | C++ 核心类型 |
| [接口设计](docs/interfaces.md) | 抽象接口（存储/计算/元数据） |
| [目录结构](docs/directory-structure.md) | 源码目录规划 |
| [开发阶段](docs/development-phases.md) | 里程碑、任务拆解、测试用例 |

开发计划与进度见 [plan.md](plan.md)。

## 项目结构

```
eugraph/
├── src/
│   ├── common/           # 公共类型、接口、工具
│   ├── storage_service/  # 存储服务（KV 引擎、图操作、事务）
│   ├── compute_service/  # 计算服务（GSQL 解析、查询规划与执行）
│   ├── metadata_service/ # 元数据服务（Label 管理、ID 分配）
│   ├── rpc/              # RPC 通信
│   └── service/          # 服务管理器
├── tests/                # 单元测试 / 集成测试
├── docs/                 # 设计文档
└── plan.md               # 开发计划
```

详细目录说明见 [docs/directory-structure.md](docs/directory-structure.md)。
