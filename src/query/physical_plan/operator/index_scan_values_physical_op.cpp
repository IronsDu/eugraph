#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"
#include "query/dataset/data_chunk.hpp"

#include <functional>
#include <optional>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> IndexScanValuesPhysicalOp::executeChunk() {
    if (!values_)
        co_return;

    // 多个索引值的匹配结果是直接拼接（没有去重语义），所以不必先收集到 vector：
    // 逐个索引值流式转发，边读边吐，内存只有一个批。
    DataChunk chunk;
    // 单列 VERTEX_REF：列取一次复用，行内不再做 columns[0]。co_yield 会把 columns
    // 整个移走，所以每次重建 chunk 后必须重新绑定（对引用赋值是拷贝赋值而非重绑定）。
    std::optional<std::reference_wrapper<Column>> out_column;
    auto resetChunk = [&] {
        chunk = DataChunk{};
        chunk.addColumn(binder::BoundTypeKind::VERTEX_REF);
        chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        out_column.emplace(chunk.columns[0]);
    };
    auto appendVid = [&](VertexId vid) { chunk.appendVertexRefRow(out_column->get(), vid); };
    resetChunk();

    for (const auto& value : *values_) {
        std::vector<PropertyValue> one{value};
        auto gen = cancellable(store_.scanVerticesByIndexId(index_id_, one));
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
    }
    if (chunk.count > 0) {
        chunk.sel = SelectionVector::identity(chunk.count);
        co_yield std::move(chunk);
    }
}
} // namespace compute
} // namespace eugraph
