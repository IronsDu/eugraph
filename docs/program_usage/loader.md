# CSV Loader 使用指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 LDBC SNB 格式的 CSV 数据导入图数据库。

设计文档见 [loader-design.md](../program_design/loader-design.md)。

---

## 启动

```bash
eugraph-loader --host 127.0.0.1 --port 9090 --data-dir ./csv-data --batch-size 500
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 127.0.0.1 | Server 地址 |
| `--port` | 9090 | Server 端口 |
| `--data-dir` | **必填** | CSV 文件所在目录 |
| `--batch-size` | 500 | 每 RPC 批次的记录数 |

---

## 数据格式

### 目录结构

```
data-dir/
├── static/          # 静态数据
└── dynamic/         # 动态数据
```

### 文件命名

- 点文件: `{label}_0_0.csv`（3 段）
- 边文件: `{srcLabel}_{edgeType}_{dstLabel}_0_0.csv`（5 段）

### CSV 格式

分隔符: `|`，首行为表头。

**点文件**：首列 `id` 为 CSV 主键（INT64），剩余列为属性值。

**边文件**：前两列为起点/终点的 CSV 主键，剩余列为边属性。

### 类型推断

采样前 N 行：尝试 `int64_t` → INT64，否则 → STRING。
