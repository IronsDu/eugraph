# EuGraph

单机图数据库，兼容 [openCypher](https://opencypher.org/) 查询语言，支持 Neo4j Bolt 协议（任意 Neo4j 官方/社区驱动可直接连接）。

## 特性

### 查询语言：openCypher（含扩展）

基于 openCypher，做了一些扩展和裁剪：

**扩展**（在 openCypher 基础上新增）

- `EXPLAIN` 前缀（`EXPLAIN MATCH ...`）
- `CALL` 存储过程 —— 支持顶层独立调用（`standaloneCall`）和子句流水线中的调用（`queryCallSt`）
- 标签转型操作符 `::`（`n::Label` / `n::Label.prop`）
- `SET` 累加赋值 `+=`
- 数值字面量：十六进制 `0x...`、八进制 `0o...`、下划线分隔（`1_000_000`）
- 关键字大小写无关（`match`、`MATCH`、`Match` 等价）

**裁剪**（openCypher 中未实现）

- `LOAD CSV`（项目另有独立的 `eugraph-loader` CSV 批量导入工具）
- `FOREACH`
- 路径模式选择器：`ANY` / `ALL` / `SHORTEST` / `SHORTESTPATH` / `ALLSHORTESTPATHS`
- `PROFILE` 关键字（`EXPLAIN` 已支持）
- 函数：`REDUCE`、`TRIM`、正则匹配操作符 `=~`
- `INF` / `NAN` 字面量

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
