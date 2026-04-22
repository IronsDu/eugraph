# EuGraph

分布式图数据库，支持 Cypher 查询语言。

## 核心特性

| 特性 | 说明 |
|------|------|
| 查询语言 | Cypher（OpenCypher 子集） |
| 事务模型 | MVCC + 2PC（分布式） |
| 隔离级别 | 可重复读（REPEATABLE_READ） |
| 部署模式 | 单机 → 分布式 |
| 架构模式 | 存算分离、sync/async 分层 |

## 技术栈

| 组件 | 选择 | 说明 |
|------|------|------|
| 语言 | C++20 | |
| 构建系统 | CMake | vcpkg 依赖管理 |
| 协程库 | folly | Meta 成熟协程库 |
| RPC | fbthrift | folly 原生支持 |
| KV 存储 | WiredTiger | 高性能、支持事务 |
| 日志 | spdlog | 高性能日志 |
| 测试 | GoogleTest | |

## 构建

### 前置条件

- C++20 编译器（GCC 13+ / Clang 15+）
- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg)

### 编译

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 运行测试

```bash
./build/eugraph_tests          # 图存储层测试
./build/metadata_tests          # 元数据服务测试
./build/query_executor_tests    # 查询执行器测试
./build/rpc_integration_tests   # RPC 集成测试
./build/cypher_parser_tests     # Cypher 解析器测试
./build/logical_plan_tests      # 逻辑计划测试
```

### 启动服务

```bash
# 服务端
./build/eugraph-server -d /path/to/data

# 客户端 Shell（RPC 模式）
./build/eugraph-shell --host ::1 --port 9090

# 客户端 Shell（嵌入式模式）
./build/eugraph-shell -d /path/to/data
```

## 文档

设计文档位于 [docs/](docs/) 目录，入口见 [docs/overview.md](docs/overview.md)。

| 文档 | 说明 |
|------|------|
| [系统架构](docs/system-architecture.md) | 分层架构、sync/async 分离、线程模型 |
| [数据模型](docs/data-model.md) | Vertex/Edge 概念、Label ID 映射 |
| [KV 编码](docs/kv-encoding.md) | Key 前缀、编码规则、查询能力 |
| [类型定义](docs/type-definitions.md) | C++ 核心类型 |
| [接口设计](docs/interfaces.md) | sync/async 接口（存储/元数据） |
| [元数据服务设计](docs/metadata-service-design.md) | AsyncGraphMetaStore、GraphSchema |
| [目录结构](docs/directory-structure.md) | 源码目录说明 |
| [Cypher 解析器](docs/cypher-parser-design.md) | ANTLR4 语法、AST 节点定义 |
| [查询引擎](docs/query-engine-design.md) | 逻辑计划、物理计划、协程执行模型 |
| [Server + Shell](docs/server-shell-design.md) | fbthrift 服务层 + 交互式 Shell |
| [开发阶段](docs/development-phases.md) | 里程碑、任务拆解 |

## 项目结构

```
eugraph/
├── src/
│   ├── common/             # 公共类型
│   ├── storage/            # 存储层（meta + data，sync + async）
│   │   ├── data/           # 图数据存储
│   │   ├── meta/           # 元数据存储
│   │   └── kv/             # KV 编解码
│   ├── compute_service/    # 计算层（Cypher 解析、查询规划与执行）
│   ├── server/             # fbthrift 服务端
│   └── shell/              # 交互式 Shell 客户端
├── tests/                  # 测试
├── docs/                   # 设计文档
└── proto/                  # Thrift IDL
```

详细目录说明见 [docs/directory-structure.md](docs/directory-structure.md)。
