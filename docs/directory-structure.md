# 目录结构

> 参见 [overview.md](overview.md) 返回文档导航

```
eugraph/
├── CMakeLists.txt
├── proto/
│   └── eugraph.thrift                   # Thrift IDL 定义
│
├── src/
│   ├── common/
│   │   └── types/
│   │       ├── graph_types.hpp          # 图类型定义 (VertexId, EdgeId, Properties, ...)
│   │       ├── constants.hpp            # 常量定义 (INVALID_*, WT_TABLE_CONFIG)
│   │       └── error.hpp               # 错误类型
│   │
│   ├── storage/                         # 存储层
│   │   ├── wt_store_base.hpp/cpp        # WtStoreBase 共享基类
│   │   ├── graph_schema.hpp             # GraphSchema 内存 schema
│   │   ├── io_scheduler.hpp             # IoScheduler (IO 线程池调度)
│   │   │
│   │   ├── data/                        # 图数据存储
│   │   │   ├── i_sync_graph_data_store.hpp   # ISyncGraphDataStore 接口
│   │   │   ├── sync_graph_data_store.hpp/cpp # SyncGraphDataStore 实现
│   │   │   ├── i_async_graph_data_store.hpp  # IAsyncGraphDataStore 接口
│   │   │   └── async_graph_data_store.hpp    # AsyncGraphDataStore 实现 (header-only)
│   │   │
│   │   ├── meta/                        # 元数据存储
│   │   │   ├── i_sync_graph_meta_store.hpp   # ISyncGraphMetaStore 接口
│   │   │   ├── sync_graph_meta_store.hpp/cpp # SyncGraphMetaStore 实现
│   │   │   ├── i_async_graph_meta_store.hpp  # IAsyncGraphMetaStore 接口
│   │   │   ├── async_graph_meta_store.hpp/cpp# AsyncGraphMetaStore 实现
│   │   │   └── meta_codec.hpp/cpp            # MetadataCodec 序列化
│   │   │
│   │   └── kv/                          # KV 编解码
│   │       ├── key_codec.hpp/cpp
│   │       └── value_codec.hpp/cpp
│   │
│   ├── compute_service/                 # 计算层 (只依赖 async 接口)
│   │   ├── parser/
│   │   │   ├── ast.hpp                  # AST 节点定义
│   │   │   ├── cypher_parser.hpp/cpp    # Cypher 解析器
│   │   │   └── generated/              # ANTLR4 生成代码
│   │   │
│   │   ├── logical_plan/
│   │   │   ├── logical_operator.hpp     # 8 种逻辑算子
│   │   │   ├── logical_plan.hpp         # LogicalPlan 容器
│   │   │   ├── logical_plan_builder.hpp/cpp
│   │   │   └── logical_plan_visitor.hpp # Visitor (优化器扩展点)
│   │   │
│   │   ├── physical_plan/
│   │   │   ├── physical_operator.hpp/cpp  # 物理算子定义与 execute()
│   │   │   └── physical_planner.hpp/cpp   # 逻辑→物理翻译
│   │   │
│   │   └── executor/
│   │       ├── query_executor.hpp/cpp   # 查询编排 (parse→plan→execute)
│   │       ├── expression_evaluator.hpp/cpp
│   │       └── row.hpp                  # Row/Schema/RowBatch/Value
│   │
│   ├── server/                          # fbthrift 服务端
│   │   ├── eugraph_server_main.cpp      # 服务入口
│   │   └── eugraph_handler.hpp/cpp      # Thrift handler
│   │
│   └── shell/                           # 交互式 Shell
│       ├── shell_main.cpp               # Shell 入口
│       ├── shell_repl.hpp/cpp           # REPL (embedded + RPC 双模式)
│       └── rpc_client.hpp/cpp           # Thrift 客户端封装
│
├── tests/
│   ├── test_wiredtiger.cpp              # WiredTiger 基础测试
│   ├── test_key_codec.cpp               # KeyCodec 测试
│   ├── test_value_codec.cpp             # ValueCodec 测试
│   ├── test_graph_store_wt.cpp          # 图存储层测试 (79 tests)
│   ├── test_metadata_service.cpp        # 元数据服务测试 (17 tests)
│   ├── test_cypher_parser.cpp           # Cypher 解析器测试
│   ├── test_logical_plan.cpp            # 逻辑计划测试
│   ├── test_query_executor.cpp          # 查询执行器测试 (47 tests)
│   └── test_rpc_integration.cpp         # RPC 集成测试 (8 tests)
│
└── docs/                                # 设计文档
```
