# 目录结构

> 参见 [overview.md](overview.md) 返回文档导航

```
eugraph/
├── CMakeLists.txt
├── src/
│   ├── common/
│   │   ├── types/
│   │   │   ├── graph_types.hpp       # 图类型定义
│   │   │   ├── error.hpp             # 错误类型
│   │   │   └── constants.hpp         # 常量定义
│   │   │
│   │   ├── interface/
│   │   │   ├── storage_interface.hpp # 存储服务接口
│   │   │   ├── compute_interface.hpp # 计算服务接口
│   │   │   └── metadata_interface.hpp# 元数据服务接口
│   │   │
│   │   └── util/
│   │       ├── logging.hpp           # 日志工具
│   │       └── config.hpp            # 配置解析
│   │
│   ├── storage_service/
│   │   ├── storage_service.hpp       # 存储服务实现
│   │   ├── graph_operations.hpp      # 图操作实现
│   │   ├── transaction_manager.hpp   # 事务管理
│   │   └── kv/
│   │       ├── kv_engine.hpp         # IKVEngine 抽象接口
│   │       ├── rocksdb_store.hpp     # RocksDB 实现
│   │       ├── key_codec.hpp         # Key 编解码
│   │       └── value_codec.hpp       # Value 编解码
│   │
│   ├── compute_service/
│   │   ├── compute_service.hpp       # 计算服务实现
│   │   ├── parser/
│   │   │   ├── lexer.hpp             # 词法分析
│   │   │   └── parser.hpp            # 语法分析
│   │   ├── planner/
│   │   │   └── query_planner.hpp     # 查询计划
│   │   └── executor/
│   │       └── query_executor.hpp    # 执行引擎
│   │
│   ├── metadata_service/
│   │   ├── metadata_service.hpp      # 元数据服务实现
│   │   ├── label_manager.hpp         # Label 管理
│   │   └── id_allocator.hpp          # ID 分配
│   │
│   ├── rpc/
│   │   ├── remote_storage_client.hpp # 远程存储客户端
│   │   ├── remote_metadata_client.hpp# 远程元数据客户端
│   │   └── serializer.hpp            # 序列化
│   │
│   ├── service/
│   │   ├── service_manager.hpp       # 服务管理器
│   │   └── service_config.hpp        # 服务配置
│   │
│   └── main.cpp                      # 入口
│
├── proto/
│   ├── storage.thrift                # 存储服务 Thrift 定义
│   ├── compute.thrift                # 计算服务 Thrift 定义
│   └── metadata.thrift               # 元数据服务 Thrift 定义
│
├── tests/
│   ├── unit/                         # 单元测试
│   └── integration/                  # 集成测试
│
├── third_party/
├── docs/
└── plan.md                           # 开发计划与进度
```
