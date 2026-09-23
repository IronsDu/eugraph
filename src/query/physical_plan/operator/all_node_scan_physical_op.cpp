#include "query/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

#include <unordered_set>

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
    auto resetChunk = [&] {
        chunk = DataChunk{};
        chunk.setSchema(output_types_);
        chunk.reserve(DataChunk::DEFAULT_CAPACITY);
    };
    auto appendVid = [&](VertexId vid) { chunk.appendRow({Value(VertexRef{vid})}); };
    resetChunk();

    if (candidate_labels_.size() <= 1) {
        auto gen = candidate_labels_.empty() ? cancellable(store_.scanAllVertices())
                                             : cancellable(store_.scanVerticesByLabel(candidate_labels_[0]));
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch) {
                appendVid(vid);
                if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
                    co_yield std::move(chunk);
                    resetChunk();
                }
            }
        }
    } else {
        struct Stream {
            folly::coro::AsyncGenerator<std::vector<VertexId>> gen;
            std::vector<VertexId> batch;
            size_t pos = 0;
            bool done = false;
        };
        std::vector<Stream> streams;
        streams.reserve(candidate_labels_.size());
        for (LabelId lid : candidate_labels_)
            streams.push_back(Stream{cancellable(store_.scanVerticesByLabel(lid)), {}, 0, false});

        const size_t k = streams.size();
        std::vector<bool> has(k, false);
        std::vector<VertexId> current(k, 0);
        while (true) {
            // 1) 让每一路都握有一个"当前 vid"（本批用完了就续下一批）
            for (size_t i = 0; i < k; ++i) {
                if (has[i] || streams[i].done)
                    continue;
                while (streams[i].pos >= streams[i].batch.size()) {
                    auto next = co_await streams[i].gen.next();
                    if (!next) {
                        streams[i].done = true;
                        break;
                    }
                    streams[i].batch = std::move(*next);
                    streams[i].pos = 0;
                }
                if (streams[i].done)
                    continue;
                current[i] = streams[i].batch[streams[i].pos];
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
                co_yield std::move(chunk);
                resetChunk();
            }
            for (size_t i = 0; i < k; ++i) {
                if (has[i] && current[i] == smallest) {
                    ++streams[i].pos;
                    has[i] = false;
                }
            }
        }
    }
    if (chunk.count > 0)
        co_yield std::move(chunk);
}
} // namespace compute
} // namespace eugraph
