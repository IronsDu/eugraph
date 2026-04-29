# EuGraph

单机图数据库，支持 Cypher 查询语言。

## 技术栈

| 组件 | 选择 |
|------|------|
| 语言 | C++20 |
| 构建系统 | CMake + vcpkg |
| 协程库 | folly |
| RPC | fbthrift |
| KV 存储 | WiredTiger |
| 日志 | spdlog |
| 测试 | GoogleTest |

## 文档

技术文档、架构设计、使用指南等详见 [docs/README.md](docs/README.md)。

## 许可证

MIT License
