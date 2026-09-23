#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "query/dataset/data_chunk.hpp"

using namespace eugraph;
using namespace eugraph::binder;

namespace {

/// 同一份 payload 分别经 typed setter 与 Value 路径写入两列，再用 getValue 读回来比对。
///
/// 两条写入路径独立、读取路径（ColumnBuffer::getValue）也独立于二者，所以这不是
/// "用同源机制验证同源机制"：typed 路径写错列、写错类型或漏掉 validity 位都会被读出来。
void expectTypedMatchesValuePath(BoundTypeKind kind, const Value& payload,
                                 const std::function<bool(Column&, size_t)>& typed_write) {
    constexpr size_t i = 0; // 每次调用都用新建的两列，无需错开行号
    Column typed = Column::flat(kind, 8);
    Column via_value = Column::flat(kind, 8);

    ASSERT_TRUE(typed_write(typed, i)) << "typed setter declined a kind it should accept";

    via_value.setValue(i, payload);

    const Value got_typed = typed.getValue(i);
    const Value got_value = via_value.getValue(i);
    ASSERT_FALSE(isNull(got_typed)) << "typed write left the row NULL";
    EXPECT_TRUE(valueEquals(got_typed, got_value) == std::optional<bool>(true))
        << "typed and Value paths disagree for kind " << static_cast<int>(kind);
}

std::vector<BoundType> vertexRefSchema() {
    return {BoundType(BoundTypeKind::VERTEX_REF, nullptr)};
}

} // namespace

/// 每个 typed setter 都必须与 setValue 写出完全一样的东西。
TEST(DataChunkTypedAccess, TypedSettersMatchTheValuePathForEveryKind) {
    expectTypedMatchesValuePath(BoundTypeKind::BOOL, Value(true),
                                [](Column& c, size_t i) { return c.setBool(i, true); });
    expectTypedMatchesValuePath(BoundTypeKind::INT64, Value(int64_t{42}),
                                [](Column& c, size_t i) { return c.setInt64(i, 42); });
    expectTypedMatchesValuePath(BoundTypeKind::DOUBLE, Value(1.5),
                                [](Column& c, size_t i) { return c.setDouble(i, 1.5); });
    expectTypedMatchesValuePath(BoundTypeKind::STRING, Value(std::string("abc")),
                                [](Column& c, size_t i) { return c.setString(i, "abc"); });
    expectTypedMatchesValuePath(BoundTypeKind::VERTEX_REF, Value(VertexRef{7}),
                                [](Column& c, size_t i) { return c.setVertexRef(i, VertexRef{7}); });
    expectTypedMatchesValuePath(BoundTypeKind::EDGE_KEY, Value(EdgeKey{3, 1, 2, 4, 5}),
                                [](Column& c, size_t i) { return c.setEdgeKey(i, EdgeKey{3, 1, 2, 4, 5}); });
    expectTypedMatchesValuePath(BoundTypeKind::PATH_TOPOLOGY, Value(mk<PathTopology>()),
                                [](Column& c, size_t i) { return c.setPathTopology(i, mk<PathTopology>()); });
    expectTypedMatchesValuePath(BoundTypeKind::VERTEX, Value(mk<VertexValue>()),
                                [](Column& c, size_t i) { return c.setVertexValue(i, mk<VertexValue>()); });
    expectTypedMatchesValuePath(BoundTypeKind::EDGE, Value(mk<EdgeValue>()),
                                [](Column& c, size_t i) { return c.setEdgeValue(i, mk<EdgeValue>()); });
    expectTypedMatchesValuePath(BoundTypeKind::PATH, Value(mk<PathValue>()),
                                [](Column& c, size_t i) { return c.setPathValue(i, mk<PathValue>()); });
    expectTypedMatchesValuePath(BoundTypeKind::LIST, Value(mk<ListValue>()),
                                [](Column& c, size_t i) { return c.setListValue(i, mk<ListValue>()); });
    expectTypedMatchesValuePath(BoundTypeKind::MAP, Value(mk<MapValue>()),
                                [](Column& c, size_t i) { return c.setMapValue(i, mk<MapValue>()); });
}

/// 重量级 payload 必须按引用接管，而不是复制一份。
TEST(DataChunkTypedAccess, TypedHandleWritesTakeThePayloadOver) {
    auto vertex = mk<VertexValue>();
    vertex->id = 11;
    Column col = Column::flat(BoundTypeKind::VERTEX, 4);
    ASSERT_TRUE(col.setVertexValue(0, vertex));
    EXPECT_EQ(std::get<VertexValuePtr>(col.getValue(0)).get(), vertex.get());
    EXPECT_EQ(vertex.use_count(), 2); // 局部变量 + 列

    auto list = mk<ListValue>();
    Column list_col = Column::flat(BoundTypeKind::LIST, 4);
    ASSERT_TRUE(list_col.setListValue(0, list));
    EXPECT_EQ(std::get<ListValuePtr>(list_col.getValue(0)).get(), list.get());
}

/// 类型不符 / DICTIONARY 列（只读）时必须拒绝，且不留下任何写入痕迹。
TEST(DataChunkTypedAccess, TypedSettersDeclineWrongKindAndDictionaryColumns) {
    Column int_col = Column::flat(BoundTypeKind::INT64, 4);
    EXPECT_FALSE(int_col.setVertexRef(0, VertexRef{1}));
    EXPECT_FALSE(int_col.setString(0, "x"));
    // 拒绝时什么都不写：该行仍是 reserve() 留下的默认 int64（0），而不是被拒的 payload
    const Value kept = int_col.getValue(0);
    ASSERT_TRUE(std::holds_alternative<int64_t>(kept));
    EXPECT_EQ(std::get<int64_t>(kept), 0);

    auto buf = std::make_shared<ColumnBuffer>();
    buf->type = BoundTypeKind::VERTEX_REF;
    buf->reserve(4);
    buf->setVertexRef(0, VertexRef{5});

    Column dict = Column::dict(buf, SelectionVector::identity(4));
    EXPECT_FALSE(dict.setVertexRef(0, VertexRef{99}));
    EXPECT_EQ(std::get<VertexRef>(dict.getValue(0)).id, 5u);
}

/// 没 reserve 就写（超出 capacity）也必须能读回来。
///
/// 旧实现里 setValid() 对越界位是静默不写，于是数据写进去了、行却读成 NULL。
TEST(DataChunkTypedAccess, TypedSettersGrowRowsPastCapacity) {
    Column col = Column::flat(BoundTypeKind::VERTEX_REF); // capacity 0
    for (VertexId vid = 1; vid <= 5; ++vid)
        ASSERT_TRUE(col.setVertexRef(vid - 1, VertexRef{vid}));

    EXPECT_FALSE(col.isNull(4));
    EXPECT_EQ(std::get<VertexRef>(col.getValue(4)).id, 5u);

    Column any = Column::flat(BoundTypeKind::STRING);
    ASSERT_TRUE(any.setString(0, "past-capacity"));
    EXPECT_EQ(std::get<std::string>(any.getValue(0)), "past-capacity");
}

/// 扫描算子用的单列追加：结果必须与旧的 appendRow({Value(...)}) 完全一致。
TEST(DataChunkTypedAccess, AppendVertexRefRowMatchesAppendRow) {
    constexpr size_t kRows = 2500; // 跨多个批，覆盖超出一次 reserve 的增长

    DataChunk typed;
    typed.setSchema(vertexRefSchema());
    typed.reserve(DataChunk::DEFAULT_CAPACITY);
    Column& out = typed.columns[0];
    for (size_t i = 0; i < kRows; ++i)
        typed.appendVertexRefRow(out, static_cast<VertexId>(i + 1));

    DataChunk legacy;
    legacy.setSchema(vertexRefSchema());
    legacy.reserve(kRows);
    for (size_t i = 0; i < kRows; ++i)
        legacy.appendRow({Value(VertexRef{static_cast<VertexId>(i + 1)})});

    ASSERT_EQ(typed.count, legacy.count);
    for (size_t i = 0; i < kRows; ++i) {
        const Value a = typed.getValue(0, i);
        const Value b = legacy.getValue(0, i);
        ASSERT_FALSE(isNull(a)) << "row " << i << " read back NULL";
        ASSERT_TRUE(valueEquals(a, b) == std::optional<bool>(true)) << "row " << i << " differs";
    }
}

/// 复现算子的"填批 → 吐批 → 重建 chunk"循环：列引用必须在重建之后重新绑定，
/// 否则后续行会写进已被 move 走的旧列（悬空）或旧 chunk。
TEST(DataChunkTypedAccess, HoistedColumnIsReboundAfterChunkRebuild) {
    DataChunk chunk;
    std::optional<std::reference_wrapper<Column>> out_column;
    auto resetChunk = [&] {
        chunk = DataChunk{};
        chunk.setSchema(vertexRefSchema());
        chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        out_column.emplace(chunk.columns[0]); // 重建后必须重新取 0 号列
    };
    auto appendVid = [&](VertexId vid) { chunk.appendVertexRefRow(out_column->get(), vid); };
    resetChunk();

    std::vector<std::vector<VertexId>> batches;
    for (VertexId vid = 1; vid <= 3 * DataChunk::DEFAULT_CAPACITY; ++vid) {
        appendVid(vid);
        if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
            chunk.sel = SelectionVector::identity(chunk.count);
            batches.push_back({});
            for (size_t i = 0; i < chunk.count; ++i)
                batches.back().push_back(std::get<VertexRef>(chunk.getValue(0, i)).id);
            resetChunk();
        }
    }
    if (chunk.count > 0) {
        chunk.sel = SelectionVector::identity(chunk.count);
        batches.push_back({});
        for (size_t i = 0; i < chunk.count; ++i)
            batches.back().push_back(std::get<VertexRef>(chunk.getValue(0, i)).id);
    }

    ASSERT_EQ(batches.size(), 3u);
    VertexId expected = 1;
    for (const auto& batch : batches) {
        ASSERT_EQ(batch.size(), DataChunk::DEFAULT_CAPACITY);
        for (VertexId vid : batch)
            EXPECT_EQ(vid, expected++);
    }
}

/// 生产者吐出的 chunk 里 count / sel.count / numRows() 必须三者一致 ——
/// 消费者直接读 sel.count 时不能再看到 0。
TEST(DataChunkTypedAccess, ProducedChunkKeepsCountAndSelectionInSync) {
    DataChunk chunk;
    chunk.setSchema(vertexRefSchema());
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);
    Column& out = chunk.columns[0];
    for (VertexId vid = 1; vid <= 10; ++vid)
        chunk.appendVertexRefRow(out, vid);
    chunk.sel = SelectionVector::identity(chunk.count);

    EXPECT_EQ(chunk.count, 10u);
    EXPECT_EQ(chunk.sel.count, 10u);
    EXPECT_EQ(chunk.numRows(), 10u);
}
