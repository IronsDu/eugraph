# 开发阶段规划

> 参见 [overview.md](overview.md) 返回文档导航

## 阶段一: 基础框架 (MVP)

### 1.1 项目骨架搭建
**目标**: 搭建基本的构建系统和目录结构

**内容**:
- CMakeLists.txt 配置
- src/ 目录结构创建
- 基础类型定义 (graph_types.hpp, error.hpp)
- 第三方依赖引入 (folly, spdlog, GoogleTest)

**完成标准**: 编译通过，能运行基础可执行文件

**测试用例**:
```cpp
TEST(ProjectSkeleton, Compilation) {
    // 验证项目可以编译
    EXPECT_TRUE(build_project());

    // 验证基础类型定义存在
    EXPECT_TRUE(fs::exists("src/common/types/graph_types.hpp"));
}

TEST(ProjectSkeleton, BasicExecutable) {
    // 验证可以编译并运行简单程序
    EXPECT_EQ(run_basic_program(), 0);
}
```

---

### 1.2 KV 存储引擎集成
**目标**: 封装 RocksDB，提供基础 KV 操作接口

**内容**:
- IKVEngine 抽象接口
- RocksDBStore 实现（连接管理、put/get/del、prefix scan、事务）
- Key/Value 编解码工具 (KeyCodec/ValueCodec)

**完成标准**:
- IKVEngine 接口定义完成
- RocksDBStore 类实现完成
- KeyCodec/ValueCodec 实现完成
- 所有操作通过单元测试

**测试用例**:
```cpp
// ==================== RocksDB 存储测试 ====================

class RocksDBStoreTest : public ::testing::Test {
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBStore> store_;

    void SetUp() override {
        test_db_path_ = "/tmp/eugraph_test_" + std::to_string(getpid());
        store_ = std::make_unique<RocksDBStore>(test_db_path_);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_db_path_);
    }

    // ==================== 基础读写测试 ====================

    TEST_F(SimplePutAndGet) {
        auto result = co_await store_->put("key1", "value1");
        EXPECT_TRUE(result);

        auto value = co_await store_->get("key1");
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), "value1");
    }

    TEST_F(SimpleDelete) {
        auto result = co_await store_->put("key1", "value1");
        EXPECT_TRUE(result);

        auto result = co_await store_->del("key1");
        EXPECT_TRUE(result);

        auto value = co_await store_->get("key1");
        EXPECT_FALSE(value.has_value());
    }

    // ==================== 游标测试 ====================

    TEST_F(ScanOperation) {
        // 插入多条数据
        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");
        co_await store_->put("key3", "value3");

        // 前缀扫描
        std::vector<std::string> results;
        auto cursor = co_await store_->scan("key");
        while (co_await cursor->valid()) {
            results.push(cursor->value());
            co_await cursor->next();
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], "value1");
        EXPECT_EQ(results[1], "value2");
        EXPECT_EQ(results[2], "value3");
    }

    // ==================== 事务测试 ====================

    TEST_F(TransactionCommit) {
        auto txn = co_await store_->beginTransaction();

        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");

        auto result = co_await store_->commit(txn);
        EXPECT_TRUE(result);

        // 验证数据已提交
        auto value1 = co_await store_->get("key1");
        auto value2 = co_await store_->get("key2");
        EXPECT_TRUE(value1.has_value());
        EXPECT_TRUE(value2.has_value());
    }

    TEST_F(TransactionRollback) {
        auto txn = co_await store_->beginTransaction();

        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");

        auto result = co_await store_->rollback(txn);
        EXPECT_TRUE(result);

        // 验证数据已回滚
        auto value1 = co_await store_->get("key1");
        auto value2 = co_await store_->get("key2");
        EXPECT_FALSE(value1.has_value());
        EXPECT_FALSE(value2.has_value());
    }
};

// ==================== Key 编解码测试 ====================

class KeyCodecTest : public ::testing::Test {
protected:
    TEST_F(VertexKeyEncode) {
        auto key = KeyCodec::encodeVertexKey(1);
        auto decoded = KeyCodec::decodeVertexKey(key);
        EXPECT_EQ(decoded, 1);
    }

    TEST_F(EdgeKeyEncode) {
        auto key = KeyCodec::encodeEdgeKey(
            1,                // vertex_id
            Direction::OUT,
            1,                // edge_label_id
            2,                // neighbor_id
            100               // edge_id
        );

        auto decoded = KeyCodec::decodeEdgeKey(key);
        EXPECT_EQ(decoded.vertex_id, 1);
        EXPECT_EQ(decoded.direction, Direction::OUT);
        EXPECT_EQ(decoded.edge_label_id, 1);
        EXPECT_EQ(decoded.neighbor_id, 2);
        EXPECT_EQ(decoded.edge_id, 100);
    }

    TEST_F(LabelIndexKeyEncode) {
        auto key = KeyCodec::encodeLabelReverseKey(1, 1);  // vertex_id, label_id
        auto decoded = KeyCodec::decodeLabelReverseKey(key);
        EXPECT_EQ(decoded.vertex_id, 1);
        EXPECT_EQ(decoded.label_id, 1);
    }
};
```

---

### 1.3 图存储层
**目标**: 实现图语义的 Vertex/Edge CRUD 操作

**内容**:
- IGraphStore 抽象接口（Vertex/Edge CRUD、标签管理、边遍历、主键查询、统计）
- GraphStoreImpl 实现（基于 IKVEngine 的图语义存储）
- Vertex 操作（增删、属性读写单个/批量、标签增删）
- Edge 操作（增删、属性读写、双向索引维护）
- 主键正反向查询
- 按标签扫描顶点、边遍历
- 事务支持

**完成标准**:
- IGraphStore 接口定义完成
- GraphStoreImpl 实现完成
- 所有操作通过单元测试

---

### 1.4 元数据服务
**目标**: 实现 Label/EdgeLabel 的 ID 映射和分配

**内容**:
- Label 名称 ↔ ID 映射
- EdgeLabel 名称 ↔ ID 映射
- ID 分配器 (原子递增)
- 元数据持久化
- MetadataService 接口实现

- Label 创建/查询/列表
- EdgeLabel 创建/查询/列表

- ID 分配

**完成标准**:
- MetadataService 类实现完成
- 所有操作通过单元测试
- 集成测试验证重启后数据恢复

**测试用例**:
```cpp
// ==================== 元数据服务测试 ====================

class MetadataServiceTest : public ::testing::Test {
protected:
    std::unique_ptr<MetadataService> meta_;

    void SetUp() override {
        meta_ = std::make_unique<MetadataService>();
    }

    // ==================== Label 测试 ====================

    TEST_F(CreateAndGetLabel) {
        // 创建标签
        auto label_id = co_await meta_->createLabel("Person",
            {{"name", PropertyType::STRING, true},
            "email");  // primary_key

        EXPECT_NE(label_id, INVALID_LABEL_ID);

        // 验证可以查询
        auto result = co_await meta_->getLabelId("Person");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), label_id);

        auto name = co_await meta_->getLabelName(label_id);
        EXPECT_TRUE(name.has_value());
        EXPECT_EQ(name.value(), "Person");
    }

    TEST_F(ListLabels) {
        co_await meta_->createLabel("Person");
        co_await meta_->createLabel("User");
        co_await meta_->createLabel("Product");

        auto labels = co_await meta_->listLabels();
        EXPECT_TRUE(labels.has_value());
        EXPECT_EQ(labels.value().size(), 3);
    }

    // ==================== EdgeLabel 测试 ====================

    TEST_F(CreateAndGetEdgeLabel) {
        auto label_id = co_await meta_->createEdgeLabel("follows");
        EXPECT_NE(label_id, INVALID_EDGE_LABEL_ID);

        auto result = co_await meta_->getEdgeLabelId("follows");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), label_id);
    }

    // ==================== ID 分配测试 ====================

    TEST_F(UniqueIdAllocation) {
        std::unordered_set<VertexId> vertex_ids;
        std::unordered_set<EdgeId> edge_ids;

        for (int i = 0; i < 100; i++) {
            auto vid = co_await meta_->nextVertexId();
            EXPECT_TRUE(vertex_ids.insert(vid).second);  // 确保唯一
            vertex_ids.insert(vid);
        }

        for (int i = 0; i < 100; i++) {
            auto eid = co_await meta_->nextEdgeId();
            EXPECT_TRUE(edge_ids.insert(eid).second);
            edge_ids.insert(eid);
        }
    }
};

// ==================== 集成测试: 重启恢复 ====================

class MetadataPersistenceTest : public ::testing::Test {
protected:
    std::string test_db_path_;

    void SetUp() override {
        test_db_path_ = "/tmp/eugraph_meta_test";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_db_path_);
    }

    TEST_F(LabelPersistence) {
        // 第一次创建
        {
            auto meta = std::make_unique<MetadataService>(test_db_path_);
            co_await meta->createLabel("Person");
        }

        // 重启后验证
        {
            auto meta = std::make_unique<MetadataService>(test_db_path_);
            auto label_id = co_await meta->getLabelId("Person");
            EXPECT_TRUE(label_id.has_value());
        }
    }
};
```

---

### 1.5 图存储服务
**目标**: 实现 Vertex/Edge 的 CRUD 操作

**内容**:
- Vertex 操作
  - insertVertex (含标签索引维护)
  - getVertex
  - updateVertexProperties
  - deleteVertex (含标签索引清理)
  - getVertexLabels / addLabelToVertex / removeLabelFromVertex
- Edge 操作
  - insertEdge (含双向索引维护)
  - getEdge
  - getOutEdges / getInEdges
  - deleteEdge
- 索引操作
  - getVertexIdsByLabel (正向索引)
  - getVertexByPrimaryKey (主键索引)
  - queryVertexIndex (属性索引)
- GraphOperations 接口实现
- StorageService 接口实现

- **完成标准**:
  - GraphOperations 类实现完成
  - StorageService 类实现完成
  - 所有操作通过单元测试
  - 集成测试验证完整 CRUD 流程
  - 集成测试验证索引正确性

**测试用例**:
```cpp
// ==================== 图存储服务测试 ====================

class GraphOperationsTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;
    TransactionId txn_id_;

    void SetUp() override {
        storage_ = std::make_unique<StorageService>();
        txn_id_ = co_await storage_->transaction().beginTransaction();
    }

    void TearDown() override {
        co_await storage_->transaction().commit(txn_id_);
    }

    // ==================== Vertex 基础操作测试 ====================

    TEST_F(InsertAndGetVertex) {
        LabelIdSet labels = {1, 2};  // Person, User

        auto vid = co_await storage_->graph().insertVertex(txn_id_, labels,
            {{"name", "Alice"}, {"age", 30}});

        EXPECT_NE(vid, INVALID_VERTEX_ID);

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_TRUE(vertex.has_value());
        EXPECT_EQ(vertex.value().id, vid);
    }

    TEST_F(UpdateVertexProperties) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        // 更新属性
        co_await storage_->graph().updateVertexProperties(txn_id_, vid,
            {{"name", "Bob"}, {"age", 25}});

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_TRUE(vertex.has_value());
        EXPECT_EQ(std::get<std::string>(vertex.value().properties["name"]), "Bob");
        EXPECT_EQ(std::get<int64_t>(vertex.value().properties["age"]), 25);
    }

    TEST_F(DeleteVertex) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {{"name", "Alice"}});

        co_await storage_->graph().deleteVertex(txn_id_, vid);

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_FALSE(vertex.has_value());
    }

    // ==================== 标签操作测试 ====================

    TEST_F(GetVertexLabels) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1, 2}, {});

        auto labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_TRUE(labels.has_value());
        EXPECT_EQ(labels.value().size(), 2);
        EXPECT_TRUE(labels.value().contains(1));
        EXPECT_TRUE(labels.value().contains(2));
    }

    TEST_F(AddAndRemoveLabel) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        // 添加标签
        co_await storage_->graph().addLabelToVertex(txn_id_, vid, 2);
        auto labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_EQ(labels.value().size(), 2);

        // 移除标签
        co_await storage_->graph().removeLabelFromVertex(txn_id_, vid, 1);
        labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_EQ(labels.value().size(), 1);
        EXPECT_TRUE(labels.value().contains(2));
    }

    // ==================== Edge 操作测试 ====================

    TEST_F(InsertAndGetEdge) {
        auto src = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        auto eid = co_await storage_->graph().insertEdge(txn_id_, src, dst, 1,
            {{"since", 2020}});

        EXPECT_NE(eid, INVALID_EDGE_ID);

        auto edge = co_await storage_->graph().getEdge(txn_id_, eid);
        EXPECT_TRUE(edge.has_value());
        EXPECT_EQ(edge.value().src_id, src);
        EXPECT_EQ(edge.value().dst_id, dst);
    }

    TEST_F(GetOutEdges) {
        auto src = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        co_await storage_->graph().insertEdge(txn_id_, src, dst1, 1, {});
        co_await storage_->graph().insertEdge(txn_id_, src, dst2, 1, {});

        auto edges = storage_->graph().getOutEdges(txn_id_, src, 1);

        std::vector<Edge> results;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) results.push(e.value());
        }

        EXPECT_EQ(results.size(), 2);
    }

    TEST_F(GetInEdges) {
        auto src1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto src2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        co_await storage_->graph().insertEdge(txn_id_, src1, dst, 1, {});
        co_await storage_->graph().insertEdge(txn_id_, src2, dst, 1, {});

        auto edges = storage_->graph().getInEdges(txn_id_, dst, 1);

        std::vector<Edge> results;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) results.push(e.value());
        }

        EXPECT_EQ(results.size(), 2);
    }

    // ==================== 索引查询测试 ====================

    TEST_F(GetVertexIdsByLabel) {
        auto vid1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto vid2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto vid3 = co_await storage_->graph().insertVertex(txn_id_, {2}, {});

        auto gen = storage_->graph().getVertexIdsByLabel(txn_id_, 1);

        std::vector<VertexId> results;
        while (auto v = co_await gen.next()) {
            if (v.hasValue()) results.push(v.value());
        }

        EXPECT_EQ(results.size(), 2);
        EXPECT_TRUE(std::find(results.begin(), results.end(), vid1) != results.end());
        EXPECT_TRUE(std::find(results.begin(), results.end(), vid2) != results.end());
    }

    TEST_F(GetVertexByPrimaryKey) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1},
            {{"email", "alice@example.com"}});
        // 注意: insertVertex 需要支持主键索引

        auto result = co_await storage_->graph().getVertexIdByPrimaryKey("alice@example.com");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), vid);
    }
};

// ==================== 集成测试: 完整流程 ====================

class GraphIntegrationTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;
    std::unique_ptr<MetadataService> meta_;

    void SetUp() override {
        meta_ = std::make_unique<MetadataService>();
        storage_ = std::make_unique<StorageService>(meta_.get());
    }

    TEST_F(FullGraphFlow) {
        // 1. 创建标签
        auto person_id = co_await meta_->createLabel("Person");
        auto follows_id = co_await meta_->createEdgeLabel("follows");

        // 2. 创建顶点
        auto txn = co_await storage_->transaction().beginTransaction();

        auto alice = co_await storage_->graph().insertVertex(txn, {person_id},
            {{"name", "Alice"}, {"email", "alice@example.com"}});
        auto bob = co_await storage_->graph().insertVertex(txn, {person_id},
            {{"name", "Bob"}, {"email", "bob@example.com"}});

        // 3. 创建边
        co_await storage_->graph().insertEdge(txn, alice, bob, follows_id,
            {{"since", 2020}});

        co_await storage_->transaction().commit(txn);

        // 4. 查询验证
        auto txn2 = co_await storage_->transaction().beginTransaction();

        // 通过主键查询
        auto alice_by_pk = co_await storage_->graph().getVertexByPrimaryKey("alice@example.com");
        EXPECT_TRUE(alice_by_pk.has_value());

        // 通过标签查询
        auto persons = storage_->graph().getVertexIdsByLabel(txn2, person_id);
        int count = 0;
        while (auto v = co_await persons.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 2);

        // 遍历边
        auto edges = storage_->graph().getOutEdges(txn2, alice, follows_id);
        count = 0;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);

        co_await storage_->transaction().commit(txn2);
    }
};
```

---

### 1.6 基础事务支持
**目标**: 实现单机 MVCC 事务

**内容**:
- 事务管理器 (TransactionManager)
  - beginTransaction
  - commit
  - rollback
  - getTransaction
- 快照管理 (SnapshotManager)
  - createSnapshot
  - getSnapshot
- MVCC 可见性控制
  - 写入时创建新版本
  - 读取时根据快照读取
- 可重复读隔离级别实现
- TransactionOperations 接口实现

- **完成标准**:
  - TransactionManager 类实现完成
  - SnapshotManager 类实现完成
  - 可重复读隔离级别正确实现
  - 所有操作通过单元测试
  - 集成测试验证并发场景

**测试用例**:
```cpp
// ==================== 事务管理器测试 ====================

class TransactionManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<TransactionManager> txn_mgr_;

    void SetUp() override {
        txn_mgr_ = std::make_unique<TransactionManager>();
    }

    TEST_F(BeginTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();
        EXPECT_NE(txn_id, 0);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn != nullptr);
        EXPECT_TRUE(txn->isActive());
    }

    TEST_F(CommitTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();

        auto result = co_await txn_mgr_->commit(txn_id);
        EXPECT_TRUE(result);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn == nullptr || !txn->isActive());
    }

    TEST_F(RollbackTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();

        auto result = co_await txn_mgr_->rollback(txn_id);
        EXPECT_TRUE(result);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn == nullptr || !txn->isActive());
    }

    TEST_F(ConcurrentTransactions) {
        auto txn1 = co_await txn_mgr_->beginTransaction();
        auto txn2 = co_await txn_mgr_->beginTransaction();

        EXPECT_NE(txn1, txn2);

        auto t1 = txn_mgr_->getTransaction(txn1);
        auto t2 = txn_mgr_->getTransaction(txn2);

        EXPECT_TRUE(t1 != t2);
    }
};

// ==================== 快照管理测试 ====================

class SnapshotManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<SnapshotManager> snap_mgr_;

    void SetUp() override {
        snap_mgr_ = std::make_unique<SnapshotManager>();
    }

    TEST_F(CreateSnapshot) {
        auto snap_id = co_await snap_mgr_->createSnapshot();
        EXPECT_NE(snap_id, 0);
    }

    TEST_F(SnapshotVisibility) {
        // 创建快照
        auto snap1 = co_await snap_mgr_->createSnapshot();

        // 写入数据
        // ... (需要与存储层集成)

        // 创建新快照
        auto snap2 = co_await snap_mgr_->createSnapshot();

        // 验证快照1 可以看到 snap1 之前的数据
        // 验证快照2 可以看到所有数据
    }
};

// ==================== MVCC 隔离级别测试 ====================

class MVCCIsolationTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;

    void SetUp() override {
        storage_ = std::make_unique<StorageService>();
    }

    TEST_F(RepeatableRead) {
        // 事务1: 写入数据
        auto txn1 = co_await storage_->transaction().beginTransaction();
        co_await storage_->graph().insertVertex(txn1, {1}, {{"name", "Alice"}});

        // 事务2: 开始事务 (应该看到 Alice)
        auto txn2 = co_await storage_->transaction().beginTransaction(
        auto v1 = co_await storage_->graph().getVertexIdsByLabel(txn2, 1);
        int count = 0;
        while (auto v = co_await v1.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);

        // 事务1: 提交
        co_await storage_->transaction().commit(txn1);

        // 事务2: 应该仍然只看到 Alice (可重复读)
        auto v2 = co_await storage_->graph().getVertexIdsByLabel(txn2, 1);
        count = 0;
        while (auto v = co_await v2.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);
    }

    TEST_F(WriteConflict) {
        // 两个事务同时修改同一个顶点
        auto txn1 = co_await storage_->transaction().beginTransaction();
        auto txn2 = co_await storage_->transaction().beginTransaction();

        auto vid = co_await storage_->graph().insertVertex(txn1, {1}, {{"value", 1}});

        // 事务2 尝试读取 (应该成功)
        auto v = co_await storage_->graph().getVertex(txn2, vid);
        EXPECT_TRUE(v.has_value());

        // 事务1 修改
        co_await storage_->graph().updateVertexProperties(txn1, vid, {{"value", 2}});

        // 事务1 提交
        auto result1 = co_await storage_->transaction().commit(txn1);
        EXPECT_TRUE(result1);

        // 事务2 尝试提交 (应该失败或需要重试)
        auto result2 = co_await storage_->transaction().commit(txn2);
        // 根据实现，可能是失败或成功
    }

    TEST_F(TransactionRollbackCorrectness) {
        auto txn = co_await storage_->transaction().beginTransaction();

        auto vid = co_await storage_->graph().insertVertex(txn, {1}, {{"name", "Alice"}});

        // 回滚
        co_await storage_->transaction().rollback(txn);

        // 验证数据不可见
        auto v = co_await storage_->graph().getVertex(txn, vid);
        EXPECT_FALSE(v.has_value());
    }
};
```

---

## 后续阶段（待规划）

### 阶段三：查询能力
- GSQL 解析器
- 查询规划与执行

### 阶段四：分布式扩展
- RPC 通信（fbthrift）
- 分布式事务（2PC）

---

## 测试执行方式

- 使用 GoogleTest 框架
- 每个功能模块有对应的单元测试
- 集成测试覆盖主要使用场景
- 使用临时目录进行测试 (RocksDB)
- 性能测试后期考虑
- 测试覆盖率目标: 80%+

## 依赖库

| 组件    | 选择         | 说明           |
| ----- | ---------- | ------------ |
| 构建系统  | CMake      | C++ 标准选择     |
| 协程库   | folly      | Meta 成熟协程库   |
| RPC   | fbthrift   | folly 原生支持   |
| KV 存储 | RocksDB | 成熟、高性能、支持事务  |
| 日志库   | spdlog     | 高性能、C++20 友好 |
| 测试框架  | GoogleTest | 成熟稳定         |
