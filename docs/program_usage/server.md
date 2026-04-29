# Server 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

---

## 启动

```bash
eugraph-server --port 9090 --data-dir ./eugraph-data --threads 4
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port, -p` | 9090 | 监听端口 |
| `--data-dir, -d` | `./eugraph-data` | 数据存储目录 |
| `--threads, -t` | 4 | 计算线程数 |

`--threads` 控制 compute 线程数（IO 线程数固定为 4）。数据文件分别在 `{data_dir}/data` 和 `{data_dir}/meta`。

## 典型流程

```bash
# 1. 启动 server
eugraph-server -d ./eugraph-data

# 2. 导入数据
eugraph-loader --data-dir ./social_network-sf0.1-CsvComposite-LongDateFormatter

# 3. 连接 shell
eugraph-shell                          # RPC 模式
eugraph-shell -d ./eugraph-data        # 嵌入式模式（无需 server）
```

## 架构参考

设计文档见 [server-shell-design.md](../program_design/server-shell-design.md)。
