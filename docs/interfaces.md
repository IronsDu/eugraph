# 接口设计

> 参见 [overview.md](overview.md) 返回文档导航

## 存储服务接口

```cpp
// src/common/interface/storage_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/Expected.h>
#include <folly/span.h>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

// 图数据操作接口
class IGraphOperations {
public:
    virtual ~IGraphOperations() = default;

    // ==================== Vertex 操作 ====================

    virtual folly::coro::Task<Result<Vertex>> getVertex(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    virtual folly::coro::Task<Result<VertexId>> insertVertex(
        TransactionId txn_id,
        const LabelIdSet& label_ids,
        const Properties& properties
    ) = 0;

    virtual folly::coro::Task<Result<void>> updateVertexProperties(
        TransactionId txn_id,
        VertexId vertex_id,
        const Properties& properties,
        bool merge = true
    ) = 0;

    virtual folly::coro::Task<Result<void>> deleteVertex(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    // ==================== Label 操作 ====================

    virtual folly::coro::Task<Result<LabelIdSet>> getVertexLabels(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    virtual folly::coro::Task<Result<void>> addLabelToVertex(
        TransactionId txn_id,
        VertexId vertex_id,
        LabelId label_id
    ) = 0;

    virtual folly::coro::Task<Result<void>> removeLabelFromVertex(
        TransactionId txn_id,
        VertexId vertex_id,
        LabelId label_id
    ) = 0;

    // ==================== 主键查询 ====================

    virtual folly::coro::Task<Result<VertexId>> getVertexIdByPrimaryKey(
        const PropertyValue& pk_value
    ) = 0;

    virtual folly::coro::Task<Result<Vertex>> getVertexByPrimaryKey(
        const PropertyValue& pk_value
    ) = 0;

    // ==================== 标签索引查询 ====================

    virtual folly::coro::AsyncGenerator<Result<VertexId>> getVertexIdsByLabel(
        TransactionId txn_id,
        LabelId label_id,
        std::optional<uint64_t> limit = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<Vertex>> getVerticesByLabel(
        TransactionId txn_id,
        LabelId label_id,
        std::optional<uint64_t> limit = std::nullopt
    ) = 0;

    // ==================== 属性索引查询 ====================

    virtual folly::coro::AsyncGenerator<Result<VertexId>> queryVertexIndex(
        TransactionId txn_id,
        LabelId label_id,
        const std::string& property,
        const PropertyValue& value
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<VertexId>> queryVertexIndexRange(
        TransactionId txn_id,
        LabelId label_id,
        const std::string& property,
        const PropertyValue& start,
        const PropertyValue& end
    ) = 0;

    // ==================== Edge 操作 ====================

    virtual folly::coro::Task<Result<Edge>> getEdge(
        TransactionId txn_id,
        EdgeId edge_id
    ) = 0;

    virtual folly::coro::Task<Result<EdgeId>> insertEdge(
        TransactionId txn_id,
        VertexId src_id,
        VertexId dst_id,
        EdgeLabelId label_id,
        const Properties& properties
    ) = 0;

    virtual folly::coro::Task<Result<void>> deleteEdge(
        TransactionId txn_id,
        EdgeId edge_id
    ) = 0;

    // ==================== 边遍历 ====================

    virtual folly::coro::AsyncGenerator<Result<Edge>> getOutEdges(
        TransactionId txn_id,
        VertexId vertex_id,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<Edge>> getInEdges(
        TransactionId txn_id,
        VertexId vertex_id,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<VertexId>> getNeighborIds(
        TransactionId txn_id,
        VertexId vertex_id,
        Direction direction,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    // ==================== 统计 ====================

    virtual folly::coro::Task<Result<uint64_t>> countVerticesByLabel(
        TransactionId txn_id,
        LabelId label_id
    ) = 0;

    virtual folly::coro::Task<Result<uint64_t>> countDegree(
        TransactionId txn_id,
        VertexId vertex_id,
        Direction direction,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;
};

// 事务操作接口
class ITransactionOperations {
public:
    virtual ~ITransactionOperations() = default;

    virtual folly::coro::Task<Result<TransactionId>> beginTransaction(
        IsolationLevel level = IsolationLevel::REPEATABLE_READ
    ) = 0;

    virtual folly::coro::Task<Result<void>> commit(TransactionId txn_id) = 0;
    virtual folly::coro::Task<Result<void>> rollback(TransactionId txn_id) = 0;
    virtual folly::coro::Task<Result<bool>> isActive(TransactionId txn_id) = 0;
};

// 存储服务聚合接口
class IStorageService {
public:
    virtual ~IStorageService() = default;

    virtual IGraphOperations& graph() = 0;
    virtual ITransactionOperations& transaction() = 0;

    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```

## 元数据服务接口

```cpp
// src/common/interface/metadata_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/Expected.h>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

class IMetadataService {
public:
    virtual ~IMetadataService() = default;

    // ==================== Label (点类型) 管理 ====================

    virtual folly::coro::Task<Result<LabelId>> createLabel(
        const LabelName& name,
        const std::vector<PropertyDef>& properties = {},
        std::optional<std::string> primary_key = std::nullopt
    ) = 0;

    virtual folly::coro::Task<Result<LabelId>> getLabelId(const LabelName& name) = 0;
    virtual folly::coro::Task<Result<LabelName>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<Result<LabelDef>> getLabelDef(const LabelName& name) = 0;
    virtual folly::coro::Task<Result<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<Result<std::vector<LabelDef>>> listLabels() = 0;

    // ==================== EdgeLabel (关系类型) 管理 ====================

    virtual folly::coro::Task<Result<EdgeLabelId>> createEdgeLabel(
        const EdgeLabelName& name,
        const std::vector<PropertyDef>& properties = {},
        bool directed = true
    ) = 0;

    virtual folly::coro::Task<Result<EdgeLabelId>> getEdgeLabelId(const EdgeLabelName& name) = 0;
    virtual folly::coro::Task<Result<EdgeLabelName>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<Result<EdgeLabelDef>> getEdgeLabelDef(const EdgeLabelName& name) = 0;
    virtual folly::coro::Task<Result<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<Result<std::vector<EdgeLabelDef>>> listEdgeLabels() = 0;

    // ==================== ID 分配 ====================

    virtual folly::coro::Task<Result<VertexId>> nextVertexId() = 0;
    virtual folly::coro::Task<Result<EdgeId>> nextEdgeId() = 0;

    // 服务生命周期
    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```

## 计算服务接口

```cpp
// src/common/interface/compute_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/Expected.h>
#include <memory>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

struct ResultRow {
    std::vector<PropertyValue> values;
};

struct ResultColumn {
    std::string name;
    std::string type;
};

class IResultSet {
public:
    virtual ~IResultSet() = default;

    virtual const std::vector<ResultColumn>& columns() const = 0;
    virtual uint64_t rowCount() const = 0;
    virtual folly::coro::AsyncGenerator<ResultRow> rows() = 0;
};

struct ExecutionStats {
    uint64_t rows_returned = 0;
    uint64_t vertices_scanned = 0;
    uint64_t edges_scanned = 0;
    std::chrono::microseconds execution_time;
};

class IComputeService {
public:
    virtual ~IComputeService() = default;

    virtual folly::coro::Task<Result<SessionId>> createSession() = 0;
    virtual folly::coro::Task<Result<void>> closeSession(SessionId session_id) = 0;

    virtual folly::coro::Task<Result<std::unique_ptr<IResultSet>>> execute(
        SessionId session_id,
        const std::string& gsql
    ) = 0;

    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```
