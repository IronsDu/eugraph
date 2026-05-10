# Shell 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

---

## 启动

```bash
eugraph-shell --host 127.0.0.1 --port 9090
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host, -h` | 127.0.0.1 | Server 地址 |
| `--port, -p` | 9090 | Server 端口 |

## Shell 命令

### 图管理

| 命令 | 说明 |
|------|------|
| `:create-graph <name>` | 创建新图空间 |
| `:drop-graph <name>` | 删除图空间（不允许删除 default） |
| `:list-graphs` | 列出所有图（graph_id、名称、创建时间） |
| `:use <name>` | 切换当前图，prompt 实时显示 |

### 标签与关系类型

| 命令 | 说明 |
|------|------|
| `:create-label <name> <prop1>:<type1> ...` | 在当前图创建标签 |
| `:create-edge-label <name> <prop1>:<type1> ...` | 在当前图创建关系类型 |
| `:list-labels` | 列出当前图的所有标签 |
| `:list-edge-labels` | 列出当前图的所有关系类型 |

### 其他

| 命令 | 说明 |
|------|------|
| `:help` | 显示帮助 |
| `:exit` / `:quit` | 退出 Shell |
| 其他（不以 `:` 开头） | 作为 Cypher 查询发送，`;` 结尾 |

## 多行输入

Cypher 查询以 `;` 结尾。未以 `;` 结尾时进入续写模式（`......> ` 提示符）。

## 交互示例

```
eugraph(default)> :create-graph social
Graph created: social (id=1)

eugraph(default)> :use social
Switched to graph 'social'

eugraph(social)> :create-label Person name:STRING age:INT64
Label created: Person (id=1)

eugraph(social)> CREATE (alice:Person {name: "Alice", age: 30});
OK

eugraph(social)> MATCH (n:Person) RETURN n.name, n.age;
+---------+-------+
| n.name  | n.age |
+---------+-------+
| "Alice" | 30    |
+---------+-------+
1 rows

eugraph(social)> :list-graphs
+----------+------------+---------------------+
| graph_id | graph_name | created_at          |
+----------+------------+---------------------+
| 0        | default    | 2026-05-10 14:30:00 |
| 1        | social     | 2026-05-10 15:00:00 |
+----------+------------+---------------------+

eugraph(social)> :exit
Bye!
```

## 架构参考

设计文档见 [server-shell-design.md](../design/server-shell-design.md)。
