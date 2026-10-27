#include "query/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

#include <functional>
#include <optional>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AllNodeScanPhysicalOp::executeChunk() {
    // 三种情况都是流式，内存只有一个批（此前把全图 vid 收进 unordered_set，
    // 首行延迟和 RSS 都随图规模线性增长）：
    //   1. 单个 label：label 前向表的游标本身就按 vid 升序且内部唯一，直接转发；
    //   2. 不限 label：走全顶点游标（vid 升序、每点一次），直接转发；
    //   3. 多个 label：各路都是"升序 + 内部唯一"，做 k 路归并去重，内存 O(label 数 × 批)。
    // 注意 candidate_labels_ 来自 static_prune_hints（WHERE n:A OR n:B 这类），
    // 是"并集剪枝"，真正的谓词过滤仍在计划里，所以并集语义不变。
    DataChunk chunk;
    // 输出只有一列 VERTEX_REF，把列取出来复用，行内不再做 columns[0]。
    // co_yield 会把 columns vector 整个移走，旧引用当场失效；而给引用赋值是拷贝赋值
    // 而不是重新绑定，所以重建 chunk 之后必须重新 emplace 一次。
    std::optional<std::reference_wrapper<Column>> out_column;
    auto resetChunk = [&] {
        chunk = DataChunk{};
        chunk.setSchema(output_types_);
        chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        out_column.emplace(chunk.columns[0]);
    };
    auto appendVid = [&](VertexId vid) { chunk.appendVertexRefRow(out_column->get(), vid); };
    resetChunk();

    if (candidate_labels_.size() <= 1) {
        std::vector<VertexId> scan_batch;
        auto gen = candidate_labels_.empty()
                       ? cancellable(store_.scanAllVertices(scan_batch))
                       : cancellable(store_.scanVerticesByLabel(candidate_labels_[0], scan_batch));
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch) {
                appendVid(vid);
                if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
                    chunk.sel = SelectionVector::identity(chunk.count);
                    co_yield std::move(chunk);
                    resetChunk();
                }
            }
        }
    } else {
        const size_t k = candidate_labels_.size();
        std::vector<std::vector<VertexId>> batches(k);
        std::vector<folly::coro::AsyncGenerator<std::span<const VertexId>>> gens;
        gens.reserve(k);
        for (size_t i = 0; i < k; ++i) {
            batches[i].reserve(1024);
            gens.push_back(cancellable(store_.scanVerticesByLabel(candidate_labels_[i], batches[i])));
        }

        std::vector<bool> has(k, false);
        std::vector<bool> done(k, false);
        std::vector<size_t> pos(k, 0);
        std::vector<VertexId> current(k, 0);
        while (true) {
            // 1) 让每一路都握有一个"当前 vid"（本批用完了就续下一批）
            for (size_t i = 0; i < k; ++i) {
                if (has[i] || done[i])
                    continue;
                while (pos[i] >= batches[i].size()) {
                    auto next = co_await gens[i].next();
                    if (!next) {
                        done[i] = true;
                        break;
                    }
                    pos[i] = 0;
                }
                if (done[i])
                    continue;
                current[i] = batches[i][pos[i]];
                has[i] = true;
            }
            // 2) 取最小 vid；所有路都空了就结束
            bool any = false;
            VertexId smallest = 0;
            for (size_t i = 0; i < k; ++i) {
                if (!has[i])
                    continue;
                if (!any || current[i] < smallest) {
                    smallest = current[i];
                    any = true;
                }
            }
            if (!any)
                break;
            // 3) 输出它，并推进所有等于它的路 —— 这一步就是跨 label 去重
            appendVid(smallest);
            if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
                chunk.sel = SelectionVector::identity(chunk.count);
                co_yield std::move(chunk);
                resetChunk();
            }
            for (size_t i = 0; i < k; ++i) {
                if (has[i] && current[i] == smallest) {
                    ++pos[i];
                    has[i] = false;
                }
            }
        }
    }
    if (chunk.count > 0) {
        chunk.sel = SelectionVector::identity(chunk.count);
        co_yield std::move(chunk);
    }
}
} // namespace compute
} // namespace eugraph
