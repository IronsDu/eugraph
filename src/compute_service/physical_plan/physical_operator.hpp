#pragma once

#include "compute_service/executor/async_graph_store.hpp"
#include "compute_service/executor/expression_evaluator.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_operator.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

// ==================== Physical Operator Base ====================

class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;
    virtual folly::coro::AsyncGenerator<RowBatch> execute() = 0;
    virtual std::string toString() const = 0;
};

// ==================== Scan Operators ====================

class AllNodeScanPhysicalOp : public PhysicalOperator {
public:
    AllNodeScanPhysicalOp(std::string variable, AsyncGraphStore& store,
                          const std::unordered_map<std::string, LabelId>& label_map)
        : variable_(std::move(variable)), store_(store), label_map_(label_map) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "AllNodeScan";
    }

private:
    std::string variable_;
    AsyncGraphStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
};

class LabelScanPhysicalOp : public PhysicalOperator {
public:
    LabelScanPhysicalOp(std::string variable, LabelId label_id, AsyncGraphStore& store)
        : variable_(std::move(variable)), label_id_(label_id), store_(store) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "LabelScan";
    }

private:
    std::string variable_;
    LabelId label_id_;
    AsyncGraphStore& store_;
};

// ==================== Expand ====================

class ExpandPhysicalOp : public PhysicalOperator {
public:
    ExpandPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                     std::optional<EdgeLabelId> label_filter, cypher::RelationshipDirection direction,
                     AsyncGraphStore& store, std::unique_ptr<PhysicalOperator> child)
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
          label_filter_(label_filter), direction_(direction), store_(store), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Expand";
    }

private:
    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    std::optional<EdgeLabelId> label_filter_;
    cypher::RelationshipDirection direction_;
    AsyncGraphStore& store_;
    std::unique_ptr<PhysicalOperator> child_;
};

// ==================== Filter ====================

class FilterPhysicalOp : public PhysicalOperator {
public:
    FilterPhysicalOp(cypher::Expression predicate, Schema schema, std::unique_ptr<PhysicalOperator> child)
        : predicate_(std::move(predicate)), schema_(std::move(schema)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Filter";
    }

private:
    cypher::Expression predicate_;
    Schema schema_;
    std::unique_ptr<PhysicalOperator> child_;
    ExpressionEvaluator evaluator_;
};

// ==================== Project ====================

class ProjectPhysicalOp : public PhysicalOperator {
public:
    struct ProjectItem {
        cypher::Expression expr;
        std::string name; // output column name
    };

    ProjectPhysicalOp(std::vector<ProjectItem> items, Schema input_schema, std::unique_ptr<PhysicalOperator> child)
        : items_(std::move(items)), input_schema_(std::move(input_schema)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Project";
    }

    const std::vector<ProjectItem>& items() const {
        return items_;
    }

private:
    std::vector<ProjectItem> items_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
    ExpressionEvaluator evaluator_;
};

// ==================== Limit ====================

class LimitPhysicalOp : public PhysicalOperator {
public:
    LimitPhysicalOp(int64_t limit, std::unique_ptr<PhysicalOperator> child) : limit_(limit), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Limit";
    }

private:
    int64_t limit_;
    std::unique_ptr<PhysicalOperator> child_;
};

// ==================== Create Operators ====================

class CreateNodePhysicalOp : public PhysicalOperator {
public:
    CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                         std::vector<std::pair<LabelId, Properties>> label_props, AsyncGraphStore& store,
                         VertexId assigned_vid, std::unique_ptr<PhysicalOperator> child = nullptr)
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)), label_props_(std::move(label_props)),
          store_(store), assigned_vid_(assigned_vid), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "CreateNode";
    }

private:
    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<std::pair<LabelId, Properties>> label_props_;
    AsyncGraphStore& store_;
    VertexId assigned_vid_;
    std::unique_ptr<PhysicalOperator> child_;
};

class CreateEdgePhysicalOp : public PhysicalOperator {
public:
    CreateEdgePhysicalOp(std::string variable, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                         EdgeId assigned_eid, AsyncGraphStore& store, std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), src_id_(src_id), dst_id_(dst_id), label_id_(label_id),
          assigned_eid_(assigned_eid), store_(store), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "CreateEdge";
    }

private:
    std::string variable_;
    VertexId src_id_;
    VertexId dst_id_;
    EdgeLabelId label_id_;
    EdgeId assigned_eid_;
    AsyncGraphStore& store_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
