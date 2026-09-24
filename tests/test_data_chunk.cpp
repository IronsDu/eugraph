#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"

#include <folly/coro/BlockingWait.h>

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

/// CONSTANT 列也必须拒绝 typed 写入。
///
/// CONSTANT 的读取只认 constant_value，写进 buffer 是读不到的隐形写入；返回 false 才能让
/// 调用方回退 setValue，把广播值改掉（这才是 appendRow 时代的行为）。
///
/// 不能只依赖"类型不匹配"：Column::constant() 不设 type（保持 NULL_TYPE），补上 type 之后
/// 才真正考验 CONSTANT 这一形态本身是否被拒绝。
TEST(DataChunkTypedAccess, TypedSettersDeclineConstantColumns) {
    Column constant = Column::constant(Value(VertexRef{1}));
    constant.type = BoundTypeKind::VERTEX_REF;
    EXPECT_FALSE(constant.setVertexRef(0, VertexRef{99}));
    EXPECT_EQ(std::get<VertexRef>(constant.getValue(0)).id, 1u);

    // 回退路径能正常改广播值
    constant.setValue(0, Value(VertexRef{99}));
    EXPECT_EQ(std::get<VertexRef>(constant.getValue(0)).id, 99u);
}

/// Column 未持有 buffer 时（默认构造 / 手工设置的列）typed setter 应自行建一个。
TEST(DataChunkTypedAccess, TypedSettersCreateBufferOnDemand) {
    Column col;
    col.type = BoundTypeKind::VERTEX_REF;
    col.form = VectorForm::FLAT;
    ASSERT_EQ(col.buffer, nullptr);

    ASSERT_TRUE(col.setVertexRef(0, VertexRef{42}));
    EXPECT_FALSE(col.isNull(0));
    EXPECT_EQ(std::get<VertexRef>(col.getValue(0)).id, 42u);
}

/// appendVertexRefRow 在列拒绝 typed 写入时必须回退到 Value 路径，
/// 结果与旧的 appendRow({Value(VertexRef{vid})}) 完全一致（含 CONSTANT / DICTIONARY）。
TEST(DataChunkTypedAccess, AppendVertexRefRowFallsBackWhenColumnDeclines) {
    // CONSTANT：两边的最终广播值必须相同
    DataChunk constant_chunk;
    constant_chunk.columns.push_back(Column::constant(Value(VertexRef{1})));
    constant_chunk.reserve(4);
    Column& constant_out = constant_chunk.columns[0];
    constant_chunk.appendVertexRefRow(constant_out, 7);

    DataChunk constant_legacy;
    constant_legacy.columns.push_back(Column::constant(Value(VertexRef{1})));
    constant_legacy.reserve(4);
    constant_legacy.appendRow({Value(VertexRef{7})});

    EXPECT_EQ(constant_chunk.count, constant_legacy.count);
    EXPECT_TRUE(valueEquals(constant_chunk.getValue(0, 0), constant_legacy.getValue(0, 0)) ==
                std::optional<bool>(true));
    EXPECT_EQ(std::get<VertexRef>(constant_chunk.getValue(0, 0)).id, 7u);

    // DICTIONARY（只读）：两边都只推进行数、不改共享数据
    auto buf = std::make_shared<ColumnBuffer>();
    buf->type = BoundTypeKind::VERTEX_REF;
    buf->reserve(4);
    buf->setVertexRef(0, VertexRef{5});

    DataChunk dict_chunk;
    dict_chunk.columns.push_back(Column::dict(buf, SelectionVector::identity(4)));
    Column& dict_out = dict_chunk.columns[0];
    dict_chunk.appendVertexRefRow(dict_out, 7);

    DataChunk dict_legacy;
    dict_legacy.columns.push_back(Column::dict(buf, SelectionVector::identity(4)));
    dict_legacy.appendRow({Value(VertexRef{7})});

    EXPECT_EQ(dict_chunk.count, dict_legacy.count);
    EXPECT_EQ(std::get<VertexRef>(dict_chunk.getValue(0, 0)).id, 5u); // 共享数据未被改
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

/// The rows→chunk helper replaced the old RowBatch bridge for the hand-built result
/// paths (index DDL, EXPLAIN, database-level DDL). Those paths used to declare every
/// column ANY; the helper now picks the kind from the values. Pin both the kinds and
/// the count/sel invariants so a regression cannot slip through silently.
TEST(DataChunkRowsToChunk, DerivesColumnKindsFromValues) {
    std::vector<Row> rows;
    rows.push_back({Value(std::string("idx_name")), Value(int64_t{7}), Value(1.5), Value(true)});
    rows.push_back({Value(std::string("other")), Value(int64_t{9}), Value(2.5), Value(false)});

    auto gen = compute::wrapRowsToChunkGenerator(std::move(rows));
    auto chunk = folly::coro::blockingWait(gen.next());
    ASSERT_TRUE(chunk.has_value());

    ASSERT_EQ(chunk->numColumns(), 4u);
    EXPECT_EQ(chunk->columns[0].type, BoundTypeKind::STRING);
    EXPECT_EQ(chunk->columns[1].type, BoundTypeKind::INT64);
    EXPECT_EQ(chunk->columns[2].type, BoundTypeKind::DOUBLE);
    EXPECT_EQ(chunk->columns[3].type, BoundTypeKind::BOOL);

    EXPECT_EQ(chunk->count, 2u);
    EXPECT_EQ(chunk->sel.count, 2u);
    EXPECT_EQ(chunk->numRows(), 2u);

    EXPECT_EQ(std::get<std::string>(chunk->getValue(0, 0)), "idx_name");
    EXPECT_EQ(std::get<int64_t>(chunk->getValue(1, 1)), 9);
    EXPECT_DOUBLE_EQ(std::get<double>(chunk->getValue(2, 0)), 1.5);
    EXPECT_TRUE(std::get<bool>(chunk->getValue(3, 0)));

    // Exactly one batch, then the generator is done.
    EXPECT_FALSE(folly::coro::blockingWait(gen.next()).has_value());
}

/// A column with no typed cell anywhere keeps ANY, and every row is readable --
/// including one that is NULL (the DDL paths emit NULL cells).
TEST(DataChunkRowsToChunk, AllNullColumnStaysAnyAndNullsReadBack) {
    std::vector<Row> rows;
    rows.push_back({Value{}, Value(int64_t{1})});
    rows.push_back({Value{}, Value(int64_t{2})});

    auto gen = compute::wrapRowsToChunkGenerator(std::move(rows));
    auto chunk = folly::coro::blockingWait(gen.next());
    ASSERT_TRUE(chunk.has_value());
    ASSERT_EQ(chunk->numColumns(), 2u);
    EXPECT_EQ(chunk->columns[0].type, BoundTypeKind::ANY);
    EXPECT_EQ(chunk->columns[1].type, BoundTypeKind::INT64);
    EXPECT_TRUE(chunk->columns[0].isNull(0));
    EXPECT_TRUE(chunk->columns[0].isNull(1));
    EXPECT_EQ(std::get<int64_t>(chunk->getValue(1, 0)), 1);
    EXPECT_EQ(chunk->count, 2u);
}

/// Empty input yields no batch at all, so callers do not have to special-case it.
TEST(DataChunkRowsToChunk, EmptyRowsYieldNothing) {
    auto gen = compute::wrapRowsToChunkGenerator({});
    EXPECT_FALSE(folly::coro::blockingWait(gen.next()).has_value());
}

// ==================== 按行直写原语（appendRowTyped） ====================
//
// edge_index_scan 曾经每行构造一个 vector<Value> 再 appendRow：vector 自身一次堆分配、
// 1~3 次扩容、appendRow 只有 const& 重载所以还是逐元素拷贝。实测 3.00 分配/行、
// 50.3 ns/行；按类型直写列是 0.00 分配/行、7.7 ns/行。
//
// 这里锁两件事：结果与 appendRow 等价（行为），以及不产生堆分配（性能契约）。

namespace {

/// Opt-in 分配计数：只在测量窗口内计数，避免影响其它用例。
std::atomic<uint64_t> g_alloc_count{0};
bool g_count_allocs = false;
struct AllocWindow {
    AllocWindow() {
        g_alloc_count.store(0, std::memory_order_relaxed);
        g_count_allocs = true;
    }
    ~AllocWindow() {
        g_count_allocs = false;
    }
    uint64_t count() const {
        return g_alloc_count.load(std::memory_order_relaxed);
    }
};

} // namespace

void* operator new(size_t n) {
    if (g_count_allocs)
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        std::abort();
    return p;
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, size_t) noexcept {
    std::free(p);
}
void* operator new[](size_t n) {
    if (g_count_allocs)
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        std::abort();
    return p;
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p, size_t) noexcept {
    std::free(p);
}

namespace {

std::vector<BoundType> edgeKeyRowSchema() {
    return {BoundType(BoundTypeKind::VERTEX_REF, nullptr), BoundType(BoundTypeKind::VERTEX_REF, nullptr),
            BoundType(BoundTypeKind::EDGE_KEY, nullptr)};
}

} // namespace

/// 直写必须与 appendRow 写出完全一样的东西（三种列的组合各验一遍）。
TEST(DataChunkRowAppend, TypedRowMatchesAppendRowForEveryColumnCombination) {
    const VertexRef src{11};
    const VertexRef dst{22};
    const EdgeKey key{33, 11, 22, 7, 9};

    // 覆盖 edge_index_scan 的三种列组合：只 src / src+dst / 全部三列。
    for (int mask = 1; mask <= 7; ++mask) {
        const bool with_src = (mask & 1) != 0;
        const bool with_dst = (mask & 2) != 0;
        const bool with_edge = (mask & 4) != 0;
        if (!with_src && !with_dst && !with_edge)
            continue;

        const size_t n_cols =
            static_cast<size_t>(with_src) + static_cast<size_t>(with_dst) + static_cast<size_t>(with_edge);
        std::vector<BoundType> schema;
        if (with_src)
            schema.push_back(BoundType(BoundTypeKind::VERTEX_REF, nullptr));
        if (with_dst)
            schema.push_back(BoundType(BoundTypeKind::VERTEX_REF, nullptr));
        if (with_edge)
            schema.push_back(BoundType(BoundTypeKind::EDGE_KEY, nullptr));

        DataChunk typed;
        typed.setSchema(schema);
        typed.reserve(8);
        typed.appendRowTyped(with_src ? &src : nullptr, with_dst ? &dst : nullptr, with_edge ? &key : nullptr);

        DataChunk legacy;
        legacy.setSchema(schema);
        legacy.reserve(8);
        std::vector<Value> values;
        if (with_src)
            values.push_back(Value(src));
        if (with_dst)
            values.push_back(Value(dst));
        if (with_edge)
            values.push_back(Value(key));
        legacy.appendRow(values);

        ASSERT_EQ(typed.count, 1u) << "mask " << mask;
        ASSERT_EQ(typed.numColumns(), n_cols) << "mask " << mask;
        for (size_t c = 0; c < n_cols; ++c) {
            const Value a = typed.getValue(c, 0);
            const Value b = legacy.getValue(c, 0);
            ASSERT_FALSE(isNull(a)) << "mask " << mask << " col " << c;
            EXPECT_TRUE(valueEquals(a, b) == std::optional<bool>(true))
                << "mask " << mask << " col " << c << " differs from appendRow";
        }

        // 缺省列不写：列数不变、其余列该行保持未写状态。
        for (size_t c = n_cols; c < typed.numColumns(); ++c)
            EXPECT_TRUE(typed.columns[c].isNull(0)) << "mask " << mask << " col " << c << " should stay unwritten";
    }
}

/// 性能契约：直写一行不得产生堆分配（这就是这次改动的全部意义）。
TEST(DataChunkRowAppend, TypedRowAppendDoesNotAllocate) {
    const VertexRef src{11};
    const VertexRef dst{22};
    const EdgeKey key{33, 11, 22, 7, 9};

    DataChunk chunk;
    chunk.setSchema(edgeKeyRowSchema());
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);

    uint64_t allocs = 0;
    {
        AllocWindow window;
        for (size_t i = 0; i < 4096 && chunk.count < DataChunk::DEFAULT_CAPACITY; ++i)
            chunk.appendRowTyped(&src, &dst, &key);
        allocs = window.count();
    }
    EXPECT_EQ(allocs, 0u) << "appendRowTyped must not allocate per row";
    EXPECT_EQ(chunk.count, DataChunk::DEFAULT_CAPACITY);
}

/// 对照：旧的每行 vector<Value> + appendRow 形状确实会分配 —— 证明上面的计数是有效的，
/// 而不是因为计数器没生效才恒为 0。
TEST(DataChunkRowAppend, LegacyPerRowVectorDoesAllocate) {
    const VertexRef src{11};
    const VertexRef dst{22};
    const EdgeKey key{33, 11, 22, 7, 9};

    DataChunk chunk;
    chunk.setSchema(edgeKeyRowSchema());
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);

    uint64_t allocs = 0;
    {
        AllocWindow window;
        for (size_t i = 0; i < 64; ++i) {
            std::vector<Value> values;
            values.push_back(Value(src));
            values.push_back(Value(dst));
            values.push_back(Value(key));
            chunk.appendRow(std::move(values));
        }
        allocs = window.count();
    }
    EXPECT_GT(allocs, 0u) << "counter failed to observe the legacy per-row vector allocations";
}
