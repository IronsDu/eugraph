# Server 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

---

## 启动

```bash
eugraph-server --thrift-port 9090 --data-dir ./eugraph-data --compute-threads 4
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--thrift-port` | 9090 | Thrift RPC 监听端口（`eugraph-shell` / `eugraph-loader` 连接此端口） |
| `--bolt-port` | 7687 | Neo4j Bolt 协议端口；`0` 表示禁用 |
| `--data-dir, -d` | `./eugraph-data` | 数据存储目录 |
| `--compute-threads` | 4 | 查询计算线程数（`QueryExecutor` 的 CPU 线程池）；仅 Bolt 模式承载查询执行，Thrift 模式下不使用 |
| `--storage-io-threads` | 4 | 存储 IO 线程数（`IoScheduler` 的 IO 线程池） |
| `--thrift-io-threads` | 4 | Thrift IO 线程数；该池同时被用作 handler 执行池 |
| `--bolt-io-threads` | 1 | Bolt EventBase 线程数 |
| `--wt-cache-size-mb` | 256 | WiredTiger data cache 大小（MB） |
| `--wt-evict-threads-max` | 4 | WiredTiger 最大 eviction 线程数 |
| `--wt-txn-sync` | fsync | WiredTiger commit 同步策略：`fsync` 或 `none` |

`--compute-threads` 控制查询计算线程池；`--storage-io-threads` 与 `--thrift-io-threads` 分别控制存储 IO 池和 Thrift IO/handler 池（均默认 4）。数据文件分别在 `{data_dir}/data` 和 `{data_dir}/meta`。

线程归属：Thrift IO 池与存储 IO 池是**两个独立线程池**。Thrift / RPC 模式下 handler 与查询执行都跑在 `--thrift-io-threads` 池上，每次存储调用跳转到 `--storage-io-threads` 池，`--compute-threads` 不参与；Bolt 模式下 session 处理被调度到 `--compute-threads` 池，再从那里跳转到存储 IO 池。详见 [运行时执行模型](../../query/engine/execution-model.md)。

> `--wt-txn-sync none` 会关闭 WiredTiger commit 级 fsync，写入更快，但崩溃时可能丢失最近一段事务。
> 适合纯导入场景；正式在线服务建议保持默认 `fsync`。

## 典型流程

```bash
# 1. 启动 server
eugraph-server -d ./eugraph-data

# 2. 导入数据
eugraph-loader --data-dir ./social_network-sf0.1-CsvComposite-LongDateFormatter

# 3. 连接 shell
eugraph-shell --host 127.0.0.1 --port 9090
```

## 架构参考

设计文档见 [server-shell-design.md](../design/server-shell-design.md)。
