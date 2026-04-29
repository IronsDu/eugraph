# Shell 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

---

## 启动

```bash
# RPC 模式（连接远程 server）
eugraph-shell --host 127.0.0.1 --port 9090

# 嵌入式模式（无需 server，直接本地运行）
eugraph-shell --data-dir ./eugraph-data
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host, -h` | 127.0.0.1 | Server 地址（RPC 模式） |
| `--port, -p` | 9090 | Server 端口（RPC 模式） |
| `--data-dir, -d` | 无 | 本地数据目录，指定后进入嵌入式模式 |

嵌入式模式下绕过 RPC，直接调用本地存储层，适用于开发和调试。

## Shell 命令

| 命令 | 说明 |
|------|------|
| `:create-label <name> <prop1>:<type1> ...` | 创建标签 |
| `:create-edge-label <name> <prop1>:<type1> ...` | 创建关系类型 |
| `:list-labels` | 列出所有标签 |
| `:list-edge-labels` | 列出所有关系类型 |
| `:help` | 显示帮助 |
| `:exit` / `:quit` | 退出 Shell |
| 其他（不以 `:` 开头） | 作为 Cypher 查询发送，`;` 结尾 |

## 多行输入

Cypher 查询以 `;` 结尾。未以 `;` 结尾时进入续写模式（`......> ` 提示符）。

## 交互示例

```
eugraph> :create-label Person name:STRING age:INT64
Label created: Person (id=1)

eugraph> CREATE (alice:Person {name: "Alice", age: 30});
OK

eugraph> MATCH (n:Person) RETURN n.name, n.age;
+---------+-------+
| n.name  | n.age |
+---------+-------+
| "Alice" | 30    |
+---------+-------+
1 rows

eugraph> :exit
Bye!
```

## 架构参考

设计文档见 [server-shell-design.md](../program_design/server-shell-design.md)。
