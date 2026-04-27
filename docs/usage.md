# Eugraph 使用指南

## 1. 启动 Server

```bash
eugraph-server --port 9090 --data-dir ./eugraph-data --threads 4
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port, -p` | 9090 | 监听端口 |
| `--data-dir, -d` | `./eugraph-data` | 数据存储目录 |
| `--threads, -t` | 4 | 计算线程数 |

## 2. 启动 Loader（批量导入 CSV）

```bash
eugraph-loader --host 127.0.0.1 --port 9090 --data-dir ./csv-data --batch-size 500
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 127.0.0.1 | Server 地址 |
| `--port` | 9090 | Server 端口 |
| `--data-dir` | **必填** | CSV 文件所在目录 |
| `--batch-size` | 500 | 每 RPC 批次的记录数 |

## 3. 启动 Shell

```bash
# RPC 模式（连接远程 server）
eugraph-shell --host 127.0.0.1 --port 9090

# 嵌入式模式（无需 server，直接本地运行）
eugraph-shell --data-dir ./eugraph-data
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host, -h` | 127.0.0.1 | Server 地址 |
| `--port, -p` | 9090 | Server 端口 |
| `--data-dir, -d` | 无 | 本地数据目录，指定后进入嵌入式模式 |

## 典型使用流程

```bash
# 1. 先启动 server
eugraph-server -d ./eugraph-data

# 2. 导入数据
eugraph-loader --data-dir ./social_network-sf0.1-CsvComposite-LongDateFormatter

# 3. 查询（两种方式任选）
eugraph-shell                          # RPC 模式连 server
eugraph-shell -d ./eugraph-data        # 或嵌入式模式直连本地
```
