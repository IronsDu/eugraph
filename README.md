# EuGraph

![OpenCypher TCK](https://img.shields.io/badge/OpenCypher%20TCK-3847%20passed%2C%200%20failed-brightgreen)
![CALL](https://img.shields.io/badge/CALL%2FProcedure-Not%20Yet-yellow)

单机图数据库：

- 支持 [openCypher](https://opencypher.org/) 查询语言，并做了若干扩展
- 实现 Neo4j Bolt 协议，任意 Neo4j 官方/社区驱动可直接连接

## 特性

### 查询语言：openCypher（含扩展）

openCypher TCK 覆盖情况：全部已执行场景通过；目前未实现的场景集中在 `CALL` / 存储过程相关能力。详细分类结果见 [docs/tests/tck-results.md](docs/tests/tck-results.md)。

**扩展**（在 openCypher 基础上新增）

- `EXPLAIN` 前缀（`EXPLAIN MATCH ...`）
- 多标签与强弱模式混合属性访问
  - **便捷模式 / 弱模式**：`n.name` 自动在所有标签（含隐藏的 `__anon__`）中查找属性；单个标签命中返回标量，多个标签同名冲突时合并为列表返回
  - **强模式 / 类型安全模式**：`n::Label` / `n::Label.prop`，在编译期校验属性是否存在，并只访问指定标签下的属性；也支持 `SET n::Label.prop = ...`
  - 支持多标签节点 `(n:A:B)`、`CREATE (n:A:B {...})`、`SET n:Label` 等
  - 详见 [docs/features/multi-label-design.md](docs/features/multi-label-design.md)
- 额外数值字面量格式：十六进制（`0xFF`）、八进制（`0o17`）、下划线分隔（`1_000_000`）—— 标准 openCypher 仅支持十进制和科学计数法

**暂未实现 / 远期规划**

- `CALL` / 存储过程
- Neo4j 扩展中的 `LOAD CSV`（项目另有独立的 `eugraph-loader` CSV 批量导入工具替代）
- Neo4j 扩展中的 `FOREACH`
- Neo4j 扩展中的 `PROFILE`（已有 `EXPLAIN`，`PROFILE` 暂未实现）

### Neo4j Bolt 协议支持

实现了 Neo4j Bolt 二进制协议（v4.4 / v5.0 / v5.1），任何 Neo4j 驱动（Python、Java、Go、JS、C#、Rust 等）或 `cypher-shell` 都能直接连接 EuGraph，无需修改客户端代码。

实测通过的客户端：Python `neo4j` 5.28.x / 5.0.0 / 4.4.0 驱动、`cypher-shell` 5.26.x。详见 [docs/service/neo4j-bolt-protocol.md](docs/service/neo4j-bolt-protocol.md)。

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
