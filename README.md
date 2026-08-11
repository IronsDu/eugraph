# EuGraph

单机图数据库，兼容 [openCypher](https://opencypher.org/) 查询语言，支持 Neo4j Bolt 协议（任意 Neo4j 官方/社区驱动可直接连接）。

## 特性

### 查询语言：openCypher（含扩展）

基于 openCypher，做了一些扩展和裁剪：

**扩展**（在 openCypher 基础上新增）

- `EXPLAIN` 前缀（`EXPLAIN MATCH ...`）
- 标签转型操作符 `::`（`n::Label` / `n::Label.prop`）
- 数值字面量：十六进制 `0x...`、八进制 `0o...`、下划线分隔（`1_000_000`）
- 关键字大小写无关（`match`、`MATCH`、`Match` 等价）

**openCypher 中未实现**

- `REDUCE`、`TRIM` 函数
- 正则匹配操作符 `=~`
- 路径模式选择器：`ANY` / `SHORTEST` / `ALL SHORTEST` 等
- `INF` / `INFINITY` / `NAN` 数值字面量

**Neo4j 扩展中未实现**（这些不在 openCypher 标准，但 Neo4j 用户可能期望）

- `LOAD CSV`（项目另有独立的 `eugraph-loader` CSV 批量导入工具替代）
- `FOREACH`
- `PROFILE`（`EXPLAIN` 已支持）

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
