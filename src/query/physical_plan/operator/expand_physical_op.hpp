#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace compute {

class ExpandPhysicalOp : public PhysicalOperator {
public:
    ExpandPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                     std::optional<std::vector<EdgeLabelId>> label_filters, cypher::RelationshipDirection direction,
                     IAsyncGraphDataStore& store, Schema input_schema, std::vector<binder::BoundType> output_types,
                     std::unique_ptr<PhysicalOperator> child,
                     std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids = {},
                     std::vector<uint16_t> edge_prop_ids = {}, std::vector<LabelId> dst_label_ids = {},
                     bool dst_bound = false, bool edge_bound = false, int dst_col_idx = -1, int edge_col_idx = -1,
                     std::vector<EdgeLabelId> full_scan_labels = {}, bool full_edge_scan = false,
                     bool neighbor_cache = false, bool all_edge_labels = false, bool probe_bound_dst = false,
                     uint64_t dst_label_scan_rows = 0)
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
          label_filters_(std::move(label_filters)), direction_(direction), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)),
          dst_label_prop_ids_(std::move(dst_label_prop_ids)), edge_prop_ids_(std::move(edge_prop_ids)),
          dst_label_ids_(std::move(dst_label_ids)), dst_bound_(dst_bound), edge_bound_(edge_bound),
          dst_col_idx_(dst_col_idx), edge_col_idx_(edge_col_idx), full_scan_labels_(std::move(full_scan_labels)),
          full_edge_scan_(full_edge_scan), all_edge_labels_(all_edge_labels), neighbor_cache_enabled_(neighbor_cache),
          probe_bound_dst_(probe_bound_dst), dst_label_scan_rows_(dst_label_scan_rows) {
        for (size_t i = 0; i < input_schema_.size(); ++i) {
            if (input_schema_[i] == src_var_) {
                src_col_idx_ = static_cast<int>(i);
                break;
            }
        }
        if (dst_bound_ && dst_col_idx_ < 0) {
            for (size_t i = 0; i < input_schema_.size(); ++i) {
                if (input_schema_[i] == dst_var_) {
                    dst_col_idx_ = static_cast<int>(i);
                    break;
                }
            }
        }
        if (edge_bound_ && edge_col_idx_ < 0) {
            for (size_t i = 0; i < input_schema_.size(); ++i) {
                if (input_schema_[i] == edge_var_) {
                    edge_col_idx_ = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;

    /// Split child chunks into bounded input batches before expanding. A single
    /// input chunk can otherwise turn into millions of output rows, which both
    /// defeats LIMIT (a chunk is materialised before it is consumed) and can
    /// exhaust memory on unbounded multi-hop walks.
    folly::coro::AsyncGenerator<DataChunk> slicedChild();

    /// Configure an allowed-destination filter. Instead of scanning every
    /// edge neighbour, Expand probes edges for the destination vertex ids
    /// resolved from `allowed values` via `index_id`.
    void setAllowedDstFilter(uint32_t index_id, EdgeLabelId edge_label) {
        allowed_dst_index_id_ = index_id;
        allowed_edge_label_ = edge_label;
    }

    void setAllowedDstValues(std::shared_ptr<std::vector<PropertyValue>> values) {
        allowed_dst_values_ = std::move(values);
    }

    /// The planner moved this Expand's destination-label check into the PE
    /// directly above it; drop the local filter (the PE performs it).
    void clearDstLabelFilter() {
        dst_label_ids_.clear();
    }
    /// Per-source cache admission ceiling. Defaults to
    /// kMaxCachedEdgesPerSource; EUGRAPH_CACHE_MAX_EDGES_PER_SOURCE allows
    /// tests to exercise the overflow fallback with a small graph.
    static size_t maxCachedEdgesPerSource();

    /// Diagnostics: neighbour-cache hits across all Expand instances. Tests
    /// use this to prove the correlated-reuse path is actually taken.
    static uint64_t cacheHitCount() noexcept;
    static void resetCacheHitCount() noexcept;

    /// Probe budgets. Both default to the constants below and are overridable
    /// per process so tests can force the exact-seek fallback on a small graph.
    static size_t maxProbeEdgesPerSource();
    static size_t maxProbeCachedEdges();

    /// Diagnostics: how many times the destination-label filter took the
    /// per-neighbour fetch path vs the forward label scan. Tests assert the
    /// cost model picks the intended path and that both answer identically.
    static uint64_t labelFetchCount() noexcept;
    static uint64_t labelScanCount() noexcept;
    /// Chunks whose destination-label test was answered from the per-operator
    /// label memo instead of re-scanning the label. Proves the memo is live.
    static uint64_t labelScanMemoHitCount() noexcept;
    static void resetLabelFilterCounts() noexcept;

    /// Diagnostics: rows that could not be answered from the destination probe
    /// and were sent to the batch exact-seek fallback. Tests use this to prove
    /// the budget-overflow path is exercised.
    static uint64_t probeFallbackCount() noexcept;
    static void resetProbeFallbackCount() noexcept;

    void setLimitHint(size_t limit) override {
        limit_hint_ = limit_hint_.has_value() ? std::min(*limit_hint_, limit) : limit;
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    /// One accumulated edge for the current chunk (source row + adjacency).
    struct EdgeEntryScratch {
        size_t src_row;
        VertexId dst_id;
        EdgeId edge_id;
        EdgeLabelId edge_label_id;
        /// For UNDIRECTED split: true when the edge was found via the OUT
        /// adjacency list (physical src = liker, dst = neighbor); false when
        /// found via IN (physical src = neighbor, dst = liker).
        bool physical_out = true;
    };

    /// Apply dst_label_ids_ to `list` in place. Shared by the normal path and
    /// the neighbour-cache path (which fills the cache with filtered entries).
    folly::coro::Task<void> filterDstLabels(std::vector<EdgeEntryScratch>& list);

    /// Bounded-destination probe: materialise the bound destination's adjacency
    /// once per batch (cached by destination id) and look up each row's source.
    /// `row_src_ids` is the chunk's source id per row. Appends one edge per
    /// matching adjacency entry so multiplicity and EdgeKey orientation match
    /// the recursive destination-bound seek. Destinations whose degree exceeds
    /// kMaxProbeEdgesPerSource are left out and seeked exactly instead.
    folly::coro::Task<void> probeBoundDst(const DataChunk& chunk, size_t input_rows,
                                          const std::vector<VertexId>& row_src_ids,
                                          std::vector<EdgeEntryScratch>& edges);
    struct ProbeEdge {
        VertexId neighbor_id;
        EdgeId edge_id;
        EdgeLabelId edge_label_id;
    };
    static constexpr size_t kMaxProbeEdgesPerSource = 16'384;
    static constexpr size_t kMaxProbeCachedEdges = 262'144;
    static constexpr size_t kMaxProbeCachedSources = 65'536;
    bool probe_bound_dst_ = false;
    bool probe_cache_disabled_ = false;
    size_t probe_cached_edge_count_ = 0;
    /// destination id → (neighbour id → matching edges). The inner map makes a
    /// row lookup O(1) even for a destination with thousands of neighbours.
    std::unordered_map<VertexId, std::unordered_map<VertexId, std::vector<ProbeEdge>>> probe_cache_;
    /// Destinations above the per-source edge budget; their rows seek exactly.
    std::unordered_set<VertexId> probe_overflow_;

    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    std::optional<std::vector<EdgeLabelId>> label_filters_;
    cypher::RelationshipDirection direction_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    int src_col_idx_ = -1;
    std::unique_ptr<PhysicalOperator> child_;
    std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids_;
    std::vector<uint16_t> edge_prop_ids_;
    std::vector<LabelId> dst_label_ids_;
    /// Total vertices across dst_label_ids_ (0 when unknown). Used to price the
    /// per-neighbour label fetch against one forward label scan.
    uint64_t dst_label_scan_rows_ = 0;
    bool dst_bound_ = false;
    bool edge_bound_ = false;
    int dst_col_idx_ = -1;
    int edge_col_idx_ = -1;
    std::vector<EdgeLabelId> full_scan_labels_;
    bool full_edge_scan_ = false;
    /// The bound pattern had no relationship type and the planner expanded it
    /// to every catalog edge label. Execution collapses these prefixes into one
    /// untyped adjacency scan; toString renders the equivalent `<any>` instead
    /// of dumping the whole label set.
    bool all_edge_labels_ = false;

    uint32_t allowed_dst_index_id_ = 0;
    EdgeLabelId allowed_edge_label_ = INVALID_EDGE_LABEL_ID;
    std::shared_ptr<std::vector<PropertyValue>> allowed_dst_values_;
    std::vector<ISyncGraphDataStore::EdgeIndexEntry> edge_scratch_;
    std::optional<size_t> limit_hint_;

    /// Bounded per-operator neighbour cache for correlated (PC) re-execution.
    /// Enabled only for read-only statements and only by the planner when this
    /// Expand sits inside a PatternComprehensionApply sub-plan: the same anchor
    /// person is expanded once per path relationship, so retries repeat the
    /// same edge scans and destination-label lookups. Entries are stored
    /// *after* dst_label_ids_ filtering, in original scan order.
    struct CachedNeighbor {
        VertexId dst_id;
        EdgeId edge_id;
        EdgeLabelId edge_label_id;
        bool physical_out;
    };
    static constexpr size_t kMaxCachedEdges = 1'000'000;
    static constexpr size_t kMaxCachedSources = 65536;
    /// A single supernode must not make the cache build a multi-megabyte
    /// vector before the global cap can reject it. Sources above this degree
    /// are not cached at all; the current chunk falls back to a direct scan.
    static constexpr size_t kMaxCachedEdgesPerSource = 16'384;
    bool neighbor_cache_enabled_ = false;
    bool neighbor_cache_disabled_ = false;
    size_t cached_edge_count_ = 0;
    std::unordered_map<VertexId, std::vector<CachedNeighbor>> neighbor_cache_;

    /// Destination-label membership memo for the forward-scan path of
    /// filterDstLabels. An Expand filters its input chunk by chunk; the scan
    /// path used to re-scan the whole label for every chunk (c11/SF1: 17 x
    /// 1,575 Company rows to keep 4,580 edges). The full vid set of a label is
    /// immutable within one statement, so it is scanned once and probed
    /// afterwards. Only labels within the budget are memoised; larger labels
    /// keep the per-chunk scan.
    std::unordered_map<LabelId, std::unordered_set<VertexId>> label_scan_memo_;
    size_t label_scan_memo_vids_ = 0;
    static constexpr size_t kMaxMemoisedLabelVids = 65'536;
    /// Memo budget, overridable per process so tests can force the uncached
    /// branch (that label then keeps the pre-memo per-chunk scan).
    static size_t maxMemoisedLabelVids();
    static std::atomic<uint64_t> cache_hit_count_;
    static std::atomic<uint64_t> label_fetch_count_;
    static std::atomic<uint64_t> label_scan_count_;
    static std::atomic<uint64_t> label_scan_memo_hit_count_;
    static std::atomic<uint64_t> probe_fallback_count_;
};

} // namespace compute
} // namespace eugraph
