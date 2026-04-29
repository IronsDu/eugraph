# 目录结构

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

```
eugraph/
├── CMakeLists.txt
├── proto/
│   └── eugraph.thrift                   # Thrift IDL 定义
│
├── grammar/
│   ├── CypherLexer.g4                   # ANTLR4 词法规则
│   └── CypherParser.g4                  # ANTLR4 语法规则
│
├── src/
│   ├── common/
│   │   └── types/
│   │       ├── graph_types.hpp          # 图类型定义 (VertexId, EdgeId, LabelDef, IndexDef, ...)
│   │       ├── constants.hpp            # 常量定义 (INVALID_*, WT_TABLE_CONFIG, vidxTable, eidxTable)
│   │       └── error.hpp               # 错误类型
│   │
│   ├── storage/                         # 存储层
│   │   ├── wt_store_base.hpp/cpp        # WtStoreBase 共享基类 (WT 连接管理、session/cursor 缓存)
│   │   ├── graph_schema.hpp             # GraphSchema 内存 schema (含 deleted_labels/edge_labels tombstone)
│   │   ├── io_scheduler.hpp             # IoScheduler (IO 线程池调度)
│   │   │
│   │   ├── data/                        # 图数据存储
│   │   │   ├── i_sync_graph_data_store.hpp    # ISyncGraphDataStore 接口
│   │   │   ├── sync_graph_data_store.hpp/cpp  # SyncGraphDataStore 实现
│   │   │   ├── i_async_graph_data_store.hpp   # IAsyncGraphDataStore 接口
│   │   │   ├── async_graph_data_store.hpp     # AsyncGraphDataStore 声明
│   │   │   └── async_graph_data_store.cpp     # AsyncGraphDataStore 实现
│   │   │
│   │   ├── meta/                        # 元数据存储
│   │   │   ├── i_sync_graph_meta_store.hpp    # ISyncGraphMetaStore 接口
│   │   │   ├── sync_graph_meta_store.hpp/cpp  # SyncGraphMetaStore 实现
│   │   │   ├── i_async_graph_meta_store.hpp   # IAsyncGraphMetaStore 接口 (含 IndexInfo, 索引元数据方法)
│   │   │   ├── async_graph_meta_store.hpp/cpp # AsyncGraphMetaStore 实现 (含 batch ID 分配)
│   │   │   └── meta_codec.hpp/cpp             # MetadataCodec 序列化 (含 IndexDef 序列化)
│   │   │
│   │   └── kv/                          # KV 编解码
│   │       ├── key_codec.hpp/cpp         # KeyCodec (多表 WT key 编码)
│   │       ├── value_codec.hpp/cpp       # ValueCodec (属性值编码)
│   │       ├── index_key_codec.hpp       # IndexKeyCodec 声明 (可排序属性值编码)
│   │       └── index_key_codec.cpp       # IndexKeyCodec 实现
│   │
│   ├── compute_service/                 # 计算层 (只依赖 async 接口)
│   │   ├── parser/
│   │   │   ├── ast.hpp                  # AST 节点定义
│   │   │   ├── cypher_parser.hpp/cpp    # Cypher 解析器 (AstBuilder + CypherQueryParser)
│   │   │   ├── index_ddl_parser.hpp     # 索引 DDL 解析器声明
│   │   │   └── index_ddl_parser.cpp     # 索引 DDL 解析器实现
│   │   │
│   │   ├── logical_plan/
│   │   │   ├── logical_operator.hpp     # 12 种逻辑算子
│   │   │   ├── logical_plan.hpp         # LogicalPlan 容器
│   │   │   ├── logical_plan_builder.hpp/cpp
│   │   │   └── logical_plan_visitor.hpp # Visitor (优化器扩展点)
│   │   │
│   │   ├── physical_plan/
│   │   │   ├── physical_operator.hpp/cpp  # 物理算子定义与 execute() (含 IndexScanPhysicalOp)
│   │   │   └── physical_planner.hpp/cpp   # 逻辑→物理翻译 (含索引扫描优化)
│   │   │
│   │   └── executor/
│   │       ├── query_executor.hpp/cpp   # 查询编排 (parse→plan→execute, StreamContext, prepareStream)
│   │       ├── expression_evaluator.hpp/cpp
│   │       └── row.hpp                  # Row/Schema/RowBatch/Value
│   │
│   ├── server/                          # fbthrift 服务端
│   │   ├── eugraph_server_main.cpp      # 服务入口
│   │   └── eugraph_handler.hpp/cpp      # Thrift handler (含流式 executeCypher, batchInsert)
│   │
│   ├── shell/                           # 交互式 Shell
│   │   ├── shell_main.cpp               # Shell 入口
│   │   ├── shell_repl.hpp/cpp           # REPL (embedded + RPC 双模式, 流式消费)
│   │   └── rpc_client.hpp/cpp           # Thrift 客户端封装 (semifuture + ClientBufferedStream)
│   │
│   ├── loader/                          # CSV 数据导入
│   │   ├── loader_main.cpp
│   │   └── csv_loader.hpp/cpp
│   │
│   └── gen-cpp2/                        # fbthrift 生成代码 (预生成提交仓库)
│
├── tests/
│   ├── test_wiredtiger.cpp              # WiredTiger 基础测试
│   ├── test_key_codec.cpp               # KeyCodec 测试
│   ├── test_value_codec.cpp             # ValueCodec 测试
│   ├── test_graph_store_wt.cpp          # 图存储层测试
│   ├── test_metadata_service.cpp        # 元数据服务测试
│   ├── test_cypher_parser.cpp           # Cypher 解析器测试
│   ├── test_logical_plan.cpp            # 逻辑计划测试
│   ├── test_query_executor.cpp          # 查询执行器测试
│   ├── test_rpc_integration.cpp         # RPC 集成测试
│   ├── test_restart_persistence.cpp     # 重启持久化测试
│   ├── test_index_key_codec.cpp         # 索引键编码测试
│   ├── test_index_store.cpp             # 索引存储操作测试
│   ├── test_index_e2e.cpp               # 索引端到端测试
│   └── test_loader_integration.cpp      # Loader 集成测试
│
└── docs/                                # 设计文档
```

## 计算层依赖约束

计算层（`compute_service/`）只依赖 async 接口：

- `IAsyncGraphDataStore` — 异步图数据访问
- `IAsyncGraphMetaStore` — 异步元数据访问

不依赖任何 sync 接口（`ISyncGraphDataStore`、`ISyncGraphMetaStore`）。
