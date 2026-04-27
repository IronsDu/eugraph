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
| 构建系统 | CMake | vcpkg 子模块依赖管理 |
| 协程库 | folly | Meta 成熟协程库 |
| RPC | fbthrift | folly 原生支持 |
| KV 存储 | WiredTiger | 高性能、支持事务 |
| 日志 | spdlog | 高性能日志 |
| 测试 | GoogleTest | |

## 文档

所有技术文档位于 [docs/](docs/) 目录，入口见 [docs/overview.md](docs/overview.md)。

- **[构建与运行](docs/build-guide.md)** — 环境准备、编译、ASan 构建、测试
- **[使用指南](docs/usage.md)** — server / shell / loader 启动参数
- **[架构设计](docs/system-architecture.md)** — 分层架构、线程模型
- **[更多文档](docs/overview.md)** — 完整文档导航

## 许可证

MIT License
