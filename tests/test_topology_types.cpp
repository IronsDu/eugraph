// Phase A tests — topology-stage types (VertexRef/EdgeKey/PathTopology),
// the BoundTypeKind additions, Value variant extension, ColumnBuffer
// support for the new types, and DataChunk::replaceColumn.
//
// These tests are intentionally narrow: Phase A is a pure infrastructure
// drop. No existing operator produces or consumes the new types yet, so
// the tests focus on structural invariants (sizes, equality, value
// round-trips) rather than end-to-end behaviour.

#include <gtest/gtest.h>

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <memory>
#include <type_traits>

using namespace eugraph;
using namespace eugraph::binder;

// ==================== TC-A01: VertexRef strong typedef ====================

TEST(TopologyTypesTest, VertexRefIsZeroOverheadStrongType) {
    // Zero overhead: same size as the underlying integer.
    static_assert(sizeof(VertexRef) == sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<VertexRef>);

    // Default-constructed ref is the invalid sentinel.
    VertexRef def;
    EXPECT_EQ(def.id, INVALID_VERTEX_ID);

    // Explicit construction works; the constructor is explicit so the
    // following would NOT compile:
    //   VertexRef implicit = 42;
    VertexRef r{42};
    EXPECT_EQ(r.id, 42u);

    VertexRef r2 = VertexRef(7);
    EXPECT_EQ(r2.id, 7u);
}

TEST(TopologyTypesTest, VertexRefEqualityAndOrder) {
    VertexRef a{1}, b{1}, c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, c);
    EXPECT_FALSE(c < a);
}

// ==================== TC-A02: EdgeKey structure ====================

TEST(TopologyTypesTest, EdgeKeyLayoutAndDefaults) {
    EdgeKey k;
    EXPECT_EQ(k.id, INVALID_EDGE_ID);
    EXPECT_EQ(k.src_id, INVALID_VERTEX_ID);
    EXPECT_EQ(k.dst_id, INVALID_VERTEX_ID);
    EXPECT_EQ(k.label_id, INVALID_EDGE_LABEL_ID);
    EXPECT_EQ(k.seq, 0u);

    // EdgeKey must remain small enough for inline column storage.
    // 8 (id) + 8 (src) + 8 (dst) + 2 (label) + 4 (seq) + padding ≤ 32.
    EXPECT_LE(sizeof(EdgeKey), 32u);
}

TEST(TopologyTypesTest, EdgeKeyConstructAndCompare) {
    EdgeKey a{100, 1, 2, 5, 0};
    EdgeKey b{100, 1, 2, 5, 0};
    EdgeKey c{101, 1, 2, 5, 0};
    EdgeKey d{100, 1, 3, 5, 0}; // dst differs
    EdgeKey e{100, 1, 2, 6, 0}; // label differs
    EdgeKey f{100, 1, 2, 5, 1}; // seq differs

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(a, e);
    EXPECT_NE(a, f);
}

// ==================== TC-A03: PathTopology basic construction ====================

TEST(TopologyTypesTest, PathTopologyEmptyAndLengthInvariants) {
    PathTopology empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.hopCount(), 0u);
    EXPECT_EQ(empty.vertexCount(), 0u);

    // Single vertex, zero hops.
    PathTopology single;
    single.vertex_ids.push_back(7);
    EXPECT_FALSE(single.empty());
    EXPECT_EQ(single.vertexCount(), 1u);
    EXPECT_EQ(single.hopCount(), 0u);
    EXPECT_EQ(single.edge_ids.size(), 0u);

    // 3-hop path: 4 vertices, 3 edges, 3 label_ids, 3 seqs.
    PathTopology three_hop;
    three_hop.vertex_ids = {1, 2, 3, 4};
    three_hop.edge_ids = {10, 11, 12};
    three_hop.edge_label_ids = {5, 5, 6};
    three_hop.seqs = {0, 0, 0};
    EXPECT_EQ(three_hop.vertexCount(), 4u);
    EXPECT_EQ(three_hop.hopCount(), 3u);
    EXPECT_EQ(three_hop.edge_label_ids.size(), three_hop.hopCount());
    EXPECT_EQ(three_hop.seqs.size(), three_hop.hopCount());
}

TEST(TopologyTypesTest, PathTopologyEquality) {
    PathTopology a, b;
    a.vertex_ids = {1, 2};
    a.edge_ids = {10};
    a.edge_label_ids = {5};
    a.seqs = {0};

    b = a;
    EXPECT_EQ(a, b);

    b.vertex_ids[1] = 99;
    EXPECT_NE(a, b);
}

// ==================== TC-A04: BoundTypeKind additions ====================

TEST(TopologyTypesTest, BoundTypeKindDistinctValues) {
    // New kinds are distinct enum entries.
    EXPECT_NE(BoundTypeKind::VERTEX_REF, BoundTypeKind::VERTEX);
    EXPECT_NE(BoundTypeKind::EDGE_KEY, BoundTypeKind::EDGE);
    EXPECT_NE(BoundTypeKind::PATH_TOPOLOGY, BoundTypeKind::PATH);
    EXPECT_NE(BoundTypeKind::VERTEX_REF, BoundTypeKind::EDGE_KEY);
    EXPECT_NE(BoundTypeKind::EDGE_KEY, BoundTypeKind::PATH_TOPOLOGY);
}

TEST(TopologyTypesTest, IsTopologyKindAndSemanticKindHelpers) {
    EXPECT_TRUE(isTopologyKind(BoundTypeKind::VERTEX_REF));
    EXPECT_TRUE(isTopologyKind(BoundTypeKind::EDGE_KEY));
    EXPECT_TRUE(isTopologyKind(BoundTypeKind::PATH_TOPOLOGY));
    EXPECT_FALSE(isTopologyKind(BoundTypeKind::VERTEX));
    EXPECT_FALSE(isTopologyKind(BoundTypeKind::STRING));

    EXPECT_TRUE(isSemanticGraphKind(BoundTypeKind::VERTEX));
    EXPECT_TRUE(isSemanticGraphKind(BoundTypeKind::EDGE));
    EXPECT_TRUE(isSemanticGraphKind(BoundTypeKind::PATH));
    EXPECT_FALSE(isSemanticGraphKind(BoundTypeKind::VERTEX_REF));
}

TEST(TopologyTypesTest, TopologyAndSemanticCounterpartsAreInverses) {
    EXPECT_EQ(topologyCounterpart(BoundTypeKind::VERTEX), BoundTypeKind::VERTEX_REF);
    EXPECT_EQ(topologyCounterpart(BoundTypeKind::EDGE), BoundTypeKind::EDGE_KEY);
    EXPECT_EQ(topologyCounterpart(BoundTypeKind::PATH), BoundTypeKind::PATH_TOPOLOGY);
    // Non-graph kinds pass through unchanged.
    EXPECT_EQ(topologyCounterpart(BoundTypeKind::STRING), BoundTypeKind::STRING);

    EXPECT_EQ(semanticCounterpart(BoundTypeKind::VERTEX_REF), BoundTypeKind::VERTEX);
    EXPECT_EQ(semanticCounterpart(BoundTypeKind::EDGE_KEY), BoundTypeKind::EDGE);
    EXPECT_EQ(semanticCounterpart(BoundTypeKind::PATH_TOPOLOGY), BoundTypeKind::PATH);
    EXPECT_EQ(semanticCounterpart(BoundTypeKind::INT64), BoundTypeKind::INT64);

    // Round-trip.
    EXPECT_EQ(semanticCounterpart(topologyCounterpart(BoundTypeKind::VERTEX)), BoundTypeKind::VERTEX);
    EXPECT_EQ(topologyCounterpart(semanticCounterpart(BoundTypeKind::VERTEX_REF)), BoundTypeKind::VERTEX_REF);
}

TEST(TopologyTypesTest, BoundTypeToStringCoversNewKinds) {
    EXPECT_EQ(BoundType::VertexRef().toString(), "VERTEX_REF");
    EXPECT_EQ(BoundType::EdgeKey().toString(), "EDGE_KEY");
    EXPECT_EQ(BoundType::PathTopology().toString(), "PATH_TOPOLOGY");
}

TEST(TopologyTypesTest, BoundTypeStaticFactoriesProduceNewKinds) {
    EXPECT_EQ(BoundType::VertexRef().kind, BoundTypeKind::VERTEX_REF);
    EXPECT_EQ(BoundType::EdgeKey().kind, BoundTypeKind::EDGE_KEY);
    EXPECT_EQ(BoundType::PathTopology().kind, BoundTypeKind::PATH_TOPOLOGY);
}

// ==================== TC-A05: Value variant holds new types ====================

TEST(TopologyTypesTest, ValueHoldsVertexRef) {
    Value v = VertexRef{42};
    ASSERT_TRUE(std::holds_alternative<VertexRef>(v));
    EXPECT_EQ(std::get<VertexRef>(v).id, 42u);

    // Round-trip through the same index of the variant: same-index equality
    // path in valueEquals must reach operator==.
    Value v2 = VertexRef{42};
    EXPECT_TRUE(*valueEquals(v, v2));

    Value v3 = VertexRef{7};
    EXPECT_FALSE(*valueEquals(v, v3));
}

TEST(TopologyTypesTest, ValueHoldsEdgeKey) {
    EdgeKey k{100, 1, 2, 5, 0};
    Value v = k;
    ASSERT_TRUE(std::holds_alternative<EdgeKey>(v));
    EXPECT_EQ(std::get<EdgeKey>(v).id, 100u);

    Value same = k;
    EXPECT_TRUE(*valueEquals(v, same));

    EdgeKey other{101, 1, 2, 5, 0};
    EXPECT_FALSE(*valueEquals(v, Value(other)));
}

TEST(TopologyTypesTest, ValueHoldsPathTopology) {
    PathTopology p;
    p.vertex_ids = {1, 2, 3};
    p.edge_ids = {10, 11};
    p.edge_label_ids = {5, 5};
    p.seqs = {0, 0};

    Value v = p;
    ASSERT_TRUE(std::holds_alternative<PathTopology>(v));
    EXPECT_EQ(std::get<PathTopology>(v).vertexCount(), 3u);

    Value same = p;
    EXPECT_TRUE(*valueEquals(v, same));
}

TEST(TopologyTypesTest, ValueHashHandlesNewTypes) {
    Value r1 = VertexRef{42};
    Value r2 = VertexRef{42};
    Value r3 = VertexRef{7};
    EXPECT_EQ(ValueHash{}(r1), ValueHash{}(r2));
    EXPECT_NE(ValueHash{}(r1), ValueHash{}(r3));

    EdgeKey k{1, 2, 3, 4, 0};
    Value ek = k;
    EXPECT_EQ(ValueHash{}(ek), ValueHash{}(Value(k)));

    PathTopology p;
    p.vertex_ids = {1, 2};
    Value pv = p;
    EXPECT_EQ(ValueHash{}(pv), ValueHash{}(Value(p)));
}

// ==================== TC-A06: ColumnBuffer supports new types ====================

TEST(ColumnBufferTest, ReservesAndReadsVertexRefColumn) {
    ColumnBuffer buf;
    buf.type = BoundTypeKind::VERTEX_REF;
    buf.reserve(4);
    EXPECT_EQ(buf.capacity, 4u);
    EXPECT_EQ(buf.vertex_ref_data.size(), 4u);

    buf.vertex_ref_data[0] = VertexRef{10};
    buf.vertex_ref_data[1] = VertexRef{20};

    Value v0 = buf.getValue(0);
    ASSERT_TRUE(std::holds_alternative<VertexRef>(v0));
    EXPECT_EQ(std::get<VertexRef>(v0).id, 10u);
    EXPECT_EQ(std::get<VertexRef>(buf.getValue(1)).id, 20u);
}

TEST(ColumnBufferTest, ReservesAndReadsEdgeKeyColumn) {
    ColumnBuffer buf;
    buf.type = BoundTypeKind::EDGE_KEY;
    buf.reserve(2);
    EXPECT_EQ(buf.edge_key_data.size(), 2u);

    buf.edge_key_data[0] = EdgeKey{5, 1, 2, 100, 0};
    Value v = buf.getValue(0);
    ASSERT_TRUE(std::holds_alternative<EdgeKey>(v));
    EXPECT_EQ(std::get<EdgeKey>(v).id, 5u);
}

TEST(ColumnBufferTest, ReservesAndReadsPathTopologyColumn) {
    ColumnBuffer buf;
    buf.type = BoundTypeKind::PATH_TOPOLOGY;
    buf.reserve(2);

    PathTopology p;
    p.vertex_ids = {1, 2, 3};
    p.edge_ids = {10, 11};
    p.edge_label_ids = {5, 5};
    p.seqs = {0, 0};
    buf.path_topology_data[0] = p;

    Value v = buf.getValue(0);
    ASSERT_TRUE(std::holds_alternative<PathTopology>(v));
    EXPECT_EQ(std::get<PathTopology>(v).vertexCount(), 3u);
}

TEST(ColumnBufferTest, SetValueRoundTripsNewTypes) {
    ColumnBuffer buf;
    buf.type = BoundTypeKind::VERTEX_REF;
    buf.reserve(2);

    buf.setValue(0, Value(VertexRef{99}));
    EXPECT_EQ(buf.vertex_ref_data[0].id, 99u);
    EXPECT_EQ(std::get<VertexRef>(buf.getValue(0)).id, 99u);

    ColumnBuffer ebuf;
    ebuf.type = BoundTypeKind::EDGE_KEY;
    ebuf.reserve(2);
    ebuf.setValue(0, Value(EdgeKey{7, 1, 2, 3, 0}));
    EXPECT_EQ(std::get<EdgeKey>(ebuf.getValue(0)).id, 7u);
}

// ==================== TC-A07: DataChunk::replaceColumn basics ====================

TEST(DataChunkReplaceColumnTest, ReplacesColumnAtGivenIndex) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::INT64);
    chunk.addColumn(BoundTypeKind::STRING);
    chunk.addColumn(BoundTypeKind::DOUBLE);
    chunk.resize(2);
    ASSERT_EQ(chunk.columns.size(), 3u);

    // Populate original column 1.
    chunk.columns[1].buffer->string_data[0] = "hello";
    chunk.columns[1].buffer->string_data[1] = "world";

    // Replace column 1 with a new INT64 column.
    Column replacement = Column::flat(BoundTypeKind::INT64, 2);
    replacement.buffer->int64_data[0] = 100;
    replacement.buffer->int64_data[1] = 200;

    auto original_buffer_ptr = chunk.columns[1].buffer.get();
    chunk.replaceColumn(1, std::move(replacement));

    ASSERT_EQ(chunk.columns.size(), 3u); // count unchanged
    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::INT64);
    EXPECT_EQ(chunk.columns[1].type, BoundTypeKind::INT64); // type upgraded in place
    EXPECT_EQ(chunk.columns[2].type, BoundTypeKind::DOUBLE);

    EXPECT_EQ(chunk.columns[1].buffer->int64_data[0], 100);
    EXPECT_EQ(chunk.columns[1].buffer->int64_data[1], 200);
    (void)original_buffer_ptr; // Old buffer is released when its shared_ptr drops.
}

TEST(DataChunkReplaceColumnTest, ConvenienceOverloadAllocatesFlatColumn) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::VERTEX_REF);
    chunk.resize(2);
    ASSERT_EQ(chunk.columns.size(), 1u);

    // Upgrade VERTEX_REF column to a VERTEX column.
    Column& upgraded = chunk.replaceColumn(0, BoundTypeKind::VERTEX, 2);
    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::VERTEX);
    ASSERT_NE(chunk.columns[0].buffer, nullptr);
    EXPECT_EQ(chunk.columns[0].buffer->capacity, 2u);
    EXPECT_EQ(upgraded.type, BoundTypeKind::VERTEX);
}

TEST(DataChunkReplaceColumnTest, OutOfRangeIndexIsNoop) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::INT64);
    chunk.resize(1);

    Column replacement = Column::flat(BoundTypeKind::STRING, 1);
    chunk.replaceColumn(5, std::move(replacement)); // out of range
    EXPECT_EQ(chunk.columns.size(), 1u);
    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::INT64); // unchanged
}

// ==================== TC-A08: replaceColumn type upgrade path ====================

TEST(DataChunkReplaceColumnTest, UpgradeVertexRefToVertex) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::VERTEX_REF);
    chunk.resize(1);

    // Original column holds a bare vertex id.
    chunk.columns[0].buffer->vertex_ref_data[0] = VertexRef{42};

    // Simulate an Enricher upgrading the column in place.
    Column vertex_col = Column::flat(BoundTypeKind::VERTEX, 1);
    vertex_col.buffer->vertex_data[0] = VertexValue{42, {}, std::nullopt, false};
    chunk.replaceColumn(0, std::move(vertex_col));

    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::VERTEX);
    Value v = chunk.getValue(0, 0);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(v));
    EXPECT_EQ(std::get<VertexValue>(v).id, 42u);
}

TEST(DataChunkReplaceColumnTest, UpgradeEdgeKeyToEdge) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::EDGE_KEY);
    chunk.resize(1);
    chunk.columns[0].buffer->edge_key_data[0] = EdgeKey{7, 1, 2, 5, 0};

    Column edge_col = Column::flat(BoundTypeKind::EDGE, 1);
    edge_col.buffer->edge_data[0] = EdgeValue{7, 1, 2, 5, 0, std::nullopt, false};
    chunk.replaceColumn(0, std::move(edge_col));

    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::EDGE);
    EXPECT_EQ(std::get<EdgeValue>(chunk.getValue(0, 0)).id, 7u);
}

TEST(DataChunkReplaceColumnTest, UpgradePathTopologyToPath) {
    DataChunk chunk;
    chunk.addColumn(BoundTypeKind::PATH_TOPOLOGY);
    chunk.resize(1);

    PathTopology pt;
    pt.vertex_ids = {1, 2};
    pt.edge_ids = {10};
    pt.edge_label_ids = {5};
    pt.seqs = {0};
    chunk.columns[0].buffer->path_topology_data[0] = pt;

    Column path_col = Column::flat(BoundTypeKind::PATH, 1);
    chunk.replaceColumn(0, std::move(path_col));
    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::PATH);
}

// ==================== TC-A09: Existing operators unaffected ====================

TEST(TopologyTypesRegressionTest, LegacyTypesStillWork) {
    // VertexValue / EdgeValue / PathValue still behave as before.
    VertexValue vv{42, {}, std::nullopt, false};
    Value v = vv;
    ASSERT_TRUE(std::holds_alternative<VertexValue>(v));
    EXPECT_EQ(std::get<VertexValue>(v).id, 42u);

    // The original column kinds still reserve / read / write.
    ColumnBuffer ibuf;
    ibuf.type = BoundTypeKind::INT64;
    ibuf.reserve(2);
    ibuf.int64_data[0] = 1;
    ibuf.int64_data[1] = 2;
    EXPECT_EQ(std::get<int64_t>(ibuf.getValue(0)), 1);
    EXPECT_EQ(std::get<int64_t>(ibuf.getValue(1)), 2);

    // DataChunk schema set still works.
    DataChunk chunk;
    chunk.setSchema({BoundType::Int64(), BoundType::String()});
    EXPECT_EQ(chunk.columns.size(), 2u);
    EXPECT_EQ(chunk.columns[0].type, BoundTypeKind::INT64);
    EXPECT_EQ(chunk.columns[1].type, BoundTypeKind::STRING);
}
