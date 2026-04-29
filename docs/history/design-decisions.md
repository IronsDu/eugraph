# 设计调整记录

> [历史记录] 参见 [overview.md](overview.md) 返回文档导航

记录项目过程中的关键设计决策变更，供后续开发参考。

| 日期 | 修改内容 | 原因 |
|------|----------|------|
| 2025-03-31 | 属性存储（X\|）Key 改用 prop_id 代替 property_name | 支持部分属性查询，字段改名无需修改数据存储 |
| 2025-03-31 | Edge 数据（D\|）改为 D\|{edge_label_id}\|{edge_id}\|{prop_id}，属性分开存储 | 支持部分属性查询，支持遍历某关系类型下所有边 |
| 2025-03-31 | 元数据 properties 改为数组，包含 prop_id 映射 | 支持属性名与 prop_id 的双向查找 |
| 2025-03-31 | 内存结构 Properties 改为按 prop_id 索引的数组 | 与 KV 存储设计一致，内存更紧凑，访问更快 |
| 2025-03-31 | 主键明确定义为全局唯一标识，每顶点一个 | 主键是顶点的全局标识，不属于任何标签 |
| 2025-03-31 | 新增主键反向索引 R\|{vertex_id} → {pk_value} | 支持从 vertex_id 反查全局主键 |
| 2025-03-31 | 移除 LabelDef 中的 primary_key_prop_id 字段 | 标签内唯一约束由 IndexDef(unique=true) 表达 |
| 2026-04-01 | KV 存储引擎从 RocksDB 切换为 WiredTiger | 提供引擎级快照隔离能力，支持后续 MVCC 实现 |
| 2026-04-01 | 引入 IKVEngine 抽象接口 + IGraphStore 图语义层 | 解耦存储引擎与图操作，支持后续替换底层引擎 |
| 2026-04-01 | 新增 putVertexProperties 批量属性设置接口 | 一次写入某标签下所有属性，减少多次单独调用 |
| 2026-04-01 | insertVertex 允许不指定标签 | 支持先创建顶点再逐步补充标签和属性的场景 |
| 2026-04-10 | 查询语言从 GSQL 切换为 Cypher（OpenCypher） | Cypher 生态更成熟，ANTLR4 语法可直接复用 |
| 2026-04-10 | 采用 ANTLR4 预生成 C++ 代码（不依赖 Java 构建） | 避免构建复杂性，预生成代码提交到仓库 |
| 2026-04-10 | AstBuilder 直接遍历 Parse Tree（不继承 BaseVisitor） | ANTLR BaseVisitor 返回 std::any，与 move-only AST 类型不兼容 |
