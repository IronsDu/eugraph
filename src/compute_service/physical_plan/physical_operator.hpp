#pragma once

#include "compute_service/executor/expression_evaluator.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_operator.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

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
    virtual std::vector<const PhysicalOperator*> children() const {
        return {};
    }
};

// ==================== Scan Operators ====================

class AllNodeScanPhysicalOp : public PhysicalOperator {
public:
    AllNodeScanPhysicalOp(std::string variable, IAsyncGraphDataStore& store,
                          const std::unordered_map<std::string, LabelId>& label_map,
                          std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), store_(store), label_map_(label_map), label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "AllNodeScan(variable=" + variable_ + ")";
    }

private:
    std::string variable_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

class LabelScanPhysicalOp : public PhysicalOperator {
public:
    LabelScanPhysicalOp(std::string variable, LabelId label_id, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), label_id_(label_id), store_(store), label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        auto it = label_defs_.find(label_id_);
        std::string label_name = (it != label_defs_.end()) ? it->second.name : std::to_string(label_id_);
        return "LabelScan(variable=" + variable_ + ", label=" + label_name + ")";
    }

private:
    std::string variable_;
    LabelId label_id_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

// ==================== Index Scan ====================

class IndexScanPhysicalOp : public PhysicalOperator {
public:
    enum class ScanMode {
        EQUALITY,
        RANGE
    };

    IndexScanPhysicalOp(std::string variable, LabelId label_id, uint16_t prop_id, ScanMode mode,
                        PropertyValue search_value, std::optional<PropertyValue> range_end, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), label_id_(label_id), prop_id_(prop_id), mode_(mode),
          search_value_(std::move(search_value)), range_end_(std::move(range_end)), store_(store),
          label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        std::string label_name = std::to_string(label_id_);
        std::string prop_name = std::to_string(prop_id_);
        auto it = label_defs_.find(label_id_);
        if (it != label_defs_.end()) {
            label_name = it->second.name;
            for (const auto& pd : it->second.properties) {
                if (pd.id == prop_id_) {
                    prop_name = pd.name;
                    break;
                }
            }
        }
        return "IndexScan(variable=" + variable_ + ", label=" + label_name + ", prop=" + prop_name + ", " +
               (mode_ == ScanMode::EQUALITY ? "EQ" : "RANGE") + ")";
    }

private:
    std::string variable_;
    LabelId label_id_;
    uint16_t prop_id_;
    ScanMode mode_;
    PropertyValue search_value_;
    std::optional<PropertyValue> range_end_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

// ==================== Expand ====================

class ExpandPhysicalOp : public PhysicalOperator {
public:
    ExpandPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                     std::optional<EdgeLabelId> label_filter, cypher::RelationshipDirection direction,
                     IAsyncGraphDataStore& store, std::unique_ptr<PhysicalOperator> child)
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
          label_filter_(label_filter), direction_(direction), store_(store), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        std::string dir;
        switch (direction_) {
        case cypher::RelationshipDirection::LEFT_TO_RIGHT:
            dir = "OUT";
            break;
        case cypher::RelationshipDirection::RIGHT_TO_LEFT:
            dir = "IN";
            break;
        case cypher::RelationshipDirection::UNDIRECTED:
            dir = "ANY";
            break;
        }
        auto s = "Expand(src=" + src_var_ + ", dst=" + dst_var_;
        if (!edge_var_.empty())
            s += ", edge=" + edge_var_;
        s += ", direction=" + dir + ")";
        return s;
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    std::optional<EdgeLabelId> label_filter_;
    cypher::RelationshipDirection direction_;
    IAsyncGraphDataStore& store_;
    std::unique_ptr<PhysicalOperator> child_;
};

// ==================== Filter ====================

class FilterPhysicalOp : public PhysicalOperator {
public:
    FilterPhysicalOp(cypher::Expression predicate, Schema schema, std::unique_ptr<PhysicalOperator> child,
                     const std::unordered_map<LabelId, LabelDef>* label_defs)
        : predicate_(std::move(predicate)), schema_(std::move(schema)), child_(std::move(child)) {
        evaluator_.setLabelDefs(label_defs);
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Filter";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
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

    ProjectPhysicalOp(std::vector<ProjectItem> items, Schema input_schema, std::unique_ptr<PhysicalOperator> child,
                      const std::unordered_map<LabelId, LabelDef>* label_defs)
        : items_(std::move(items)), input_schema_(std::move(input_schema)), child_(std::move(child)) {
        evaluator_.setLabelDefs(label_defs);
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        std::string s;
        for (size_t i = 0; i < items_.size(); i++) {
            if (i > 0)
                s += ", ";
            s += items_[i].name;
        }
        return "Project(items=[" + s + "])";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
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
        return "Limit(" + std::to_string(limit_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    int64_t limit_;
    std::unique_ptr<PhysicalOperator> child_;
};

// ==================== Create Operators ====================

class CreateNodePhysicalOp : public PhysicalOperator {
public:
    CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                         std::vector<std::pair<LabelId, Properties>> label_props, IAsyncGraphDataStore& store,
                         VertexId assigned_vid, std::unique_ptr<PhysicalOperator> child = nullptr,
                         const std::unordered_map<LabelId, LabelDef>* label_defs = nullptr)
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)), label_props_(std::move(label_props)),
          store_(store), assigned_vid_(assigned_vid), child_(std::move(child)), label_defs_(label_defs) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        std::string s;
        for (size_t i = 0; i < label_ids_.size(); i++) {
            if (i > 0)
                s += ", ";
            if (label_defs_) {
                auto it = label_defs_->find(label_ids_[i]);
                s += (it != label_defs_->end()) ? it->second.name : std::to_string(label_ids_[i]);
            } else {
                s += std::to_string(label_ids_[i]);
            }
        }
        return "CreateNode(variable=" + variable_ + ", labels=[" + s + "])";
    }
    std::vector<const PhysicalOperator*> children() const override {
        if (child_)
            return {child_.get()};
        return {};
    }

private:
    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<std::pair<LabelId, Properties>> label_props_;
    IAsyncGraphDataStore& store_;
    VertexId assigned_vid_;
    std::unique_ptr<PhysicalOperator> child_;
    const std::unordered_map<LabelId, LabelDef>* label_defs_;
};

class CreateEdgePhysicalOp : public PhysicalOperator {
public:
    CreateEdgePhysicalOp(std::string variable, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                         EdgeId assigned_eid, IAsyncGraphDataStore& store, std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), src_id_(src_id), dst_id_(dst_id), label_id_(label_id),
          assigned_eid_(assigned_eid), store_(store), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "CreateEdge(variable=" + variable_ + ", src=" + std::to_string(src_id_) +
               ", dst=" + std::to_string(dst_id_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string variable_;
    VertexId src_id_;
    VertexId dst_id_;
    EdgeLabelId label_id_;
    EdgeId assigned_eid_;
    IAsyncGraphDataStore& store_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
