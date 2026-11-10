#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Leaf physical operator for EXISTS sub-plans. Produces a single-row DataChunk
/// with correlated variable values injected from the outer SemiJoinPhysicalOp
/// before each execution.
class CorrelatedSourcePhysicalOp : public PhysicalOperator {
public:
    CorrelatedSourcePhysicalOp(std::vector<std::string> variables, std::vector<binder::BoundType> types)
        : variables_(std::move(variables)), types_(std::move(types)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "CorrelatedSource";
    }

    /// Set the correlated values for the next execution.
    void setValues(const std::vector<Value>& values) {
        view_chunk_ = nullptr;
        view_cols_.clear();
        chunks_ = nullptr;
        rows_.clear();
        rows_.push_back(values);
    }
    void setRows(std::vector<std::vector<Value>> rows) {
        view_chunk_ = nullptr;
        view_cols_.clear();
        chunks_ = nullptr;
        rows_ = std::move(rows);
    }

    /// Replay a materialised producer's chunks verbatim (shared-anchor fork).
    /// The chunks must match the schema this source was planned with; column
    /// buffers are shared, not copied. Used by
    /// PatternComprehensionForkPhysicalOp to feed the anchor's rows into every
    /// branch without re-executing the anchor.
    void setChunks(std::shared_ptr<const std::vector<DataChunk>> chunks) {
        view_chunk_ = nullptr;
        view_cols_.clear();
        rows_.clear();
        chunks_ = std::move(chunks);
        chunk_index_ = 0;
    }

    /// Inject one row from an already-evaluated chunk without materialising a
    /// vector<Value> per left row. The chunk and row must stay alive until the
    /// next setValues()/setRows()/setRowFromChunk() or until executeChunk()
    /// has consumed the row (Apply/PCApply/LeftJoin/SemiJoin all guarantee
    /// this by draining the right side before advancing the left row).
    void setRowFromChunk(const DataChunk& chunk, size_t row, std::span<const uint32_t> columns) {
        chunks_ = nullptr;
        rows_.clear();
        view_chunk_ = &chunk;
        view_row_ = row;
        view_cols_.clear();
        view_cols_.reserve(columns.size());
        for (uint32_t col : columns) {
            if (col < chunk.columns.size())
                view_cols_.push_back(col);
        }
    }

    /// Expose the number of correlated columns.
    size_t numColumns() const {
        return variables_.size();
    }

private:
    std::vector<std::string> variables_;
    std::vector<binder::BoundType> types_;
    std::vector<std::vector<Value>> rows_;
    std::shared_ptr<const std::vector<DataChunk>> chunks_;
    size_t chunk_index_ = 0;
    const DataChunk* view_chunk_ = nullptr;
    size_t view_row_ = 0;
    std::vector<uint32_t> view_cols_;
    std::vector<Value> view_values_;
};

} // namespace compute
} // namespace eugraph
