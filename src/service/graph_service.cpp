#include "service/graph_service.hpp"

#include "query/physical_plan/physical_operator_base.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace eugraph {
namespace service {

GraphInstance* GraphService::resolveGraph(const std::string& name) {
    auto* inst = gm_.getGraph(name);
    if (!inst)
        throw std::runtime_error("Graph not found: " + name);
    return inst;
}

GraphEntry GraphService::createGraph(const std::string& name) {
    return gm_.createGraph(name);
}

bool GraphService::dropGraph(const std::string& name) {
    return gm_.dropGraph(name);
}

std::vector<GraphEntry> GraphService::listGraphs() {
    return gm_.listGraphs();
}

folly::coro::Task<LabelDef> GraphService::createLabel(const std::string& name,
                                                      const std::vector<PropertyDef>& properties,
                                                      const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);
    auto label_id = co_await inst->async_meta->createLabel(name, properties);
    if (label_id == INVALID_LABEL_ID) {
        LabelDef def;
        def.id = INVALID_LABEL_ID;
        def.name = name;
        co_return def;
    }
    co_await inst->async_data->createLabel(label_id);

    LabelDef def;
    def.id = label_id;
    def.name = name;
    def.properties = properties;
    co_return def;
}

folly::coro::Task<std::vector<LabelDef>> GraphService::listLabels(const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);
    auto labels = co_await inst->async_meta->listLabels();
    std::vector<LabelDef> result;
    for (const auto& l : labels) {
        if (l.name != kAnonLabelName)
            result.push_back(l);
    }
    co_return result;
}

folly::coro::Task<EdgeLabelDef> GraphService::createEdgeLabel(const std::string& name,
                                                              const std::vector<PropertyDef>& properties,
                                                              const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);
    auto label_id = co_await inst->async_meta->createEdgeLabel(name, properties);
    if (label_id == INVALID_EDGE_LABEL_ID) {
        EdgeLabelDef def;
        def.id = INVALID_EDGE_LABEL_ID;
        def.name = name;
        co_return def;
    }
    co_await inst->async_data->createEdgeLabel(label_id);

    EdgeLabelDef def;
    def.id = label_id;
    def.name = name;
    def.properties = properties;
    def.directed = true;
    co_return def;
}

folly::coro::Task<std::vector<EdgeLabelDef>> GraphService::listEdgeLabels(const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);
    auto labels = co_await inst->async_meta->listEdgeLabels();
    co_return labels;
}

folly::coro::Task<CypherExecutionContext>
GraphService::executeCypher(const std::string& query, const std::unordered_map<std::string, Value>& params,
                            const std::string& graph_name) {
    // Check for database DDL before resolving a specific graph.
    // Database-level DDL (CREATE/DROP/SHOW DATABASE, USE) operates on the
    // GraphManager, not on a single graph instance.
    auto ddl_stmt = DatabaseDdlParser::tryParse(query);
    if (ddl_stmt.has_value()) {
        auto* default_inst = resolveGraph(GraphManager::kDefaultGraphName);
        co_return co_await handleDatabaseDdl(*ddl_stmt, *default_inst->async_data);
    }

    auto* inst = resolveGraph(graph_name);

    auto ctx = co_await inst->executor->prepareStream(query, params);

    if (!ctx->error.empty()) {
        throw std::runtime_error(ctx->error);
    }

    auto labels = co_await inst->async_meta->listLabels();
    auto edge_labels = co_await inst->async_meta->listEdgeLabels();

    CypherExecutionContext result;
    result.ctx = std::move(ctx);
    for (const auto& l : labels)
        result.label_defs[l.id] = l;
    for (const auto& el : edge_labels)
        result.edge_label_defs[el.id] = el;

    co_return result;
}

folly::coro::Task<std::vector<VertexId>> GraphService::batchInsertVertices(const std::string& label_name,
                                                                           std::vector<BatchVertexEntry> entries,
                                                                           const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);

    auto label_id_opt = co_await inst->async_meta->getLabelId(label_name);
    if (!label_id_opt.has_value()) {
        throw std::runtime_error("Label not found: " + label_name);
    }
    LabelId label_id = *label_id_opt;

    auto count = entries.size();
    VertexId start_vid = co_await inst->async_meta->nextVertexIdRange(count);

    std::vector<IAsyncGraphDataStore::BatchVertexEntry> batch_entries;
    batch_entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        IAsyncGraphDataStore::BatchVertexEntry entry;
        entry.vid = start_vid + i;
        for (auto& pv : entries[i].props)
            entry.props.push_back(std::optional<PropertyValue>(std::move(pv)));
        batch_entries.push_back(std::move(entry));
    }

    co_await inst->async_data->batchInsertVertices(label_id, std::move(batch_entries));

    std::vector<VertexId> result;
    result.reserve(count);
    for (size_t i = 0; i < count; i++)
        result.push_back(start_vid + i);
    co_return result;
}

folly::coro::Task<int32_t> GraphService::batchInsertEdges(const std::string& edge_label_name,
                                                          std::vector<BatchEdgeEntry> entries,
                                                          const std::string& graph_name) {
    auto* inst = resolveGraph(graph_name);

    auto elabel_id_opt = co_await inst->async_meta->getEdgeLabelId(edge_label_name);
    if (!elabel_id_opt.has_value()) {
        throw std::runtime_error("EdgeLabel not found: " + edge_label_name);
    }
    EdgeLabelId elabel_id = *elabel_id_opt;

    auto count = entries.size();
    EdgeId start_eid = co_await inst->async_meta->nextEdgeIdRange(count);

    std::vector<IAsyncGraphDataStore::BatchEdgeEntry> batch_entries;
    batch_entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        IAsyncGraphDataStore::BatchEdgeEntry entry;
        entry.eid = start_eid + i;
        entry.src_id = entries[i].src_id;
        entry.dst_id = entries[i].dst_id;
        entry.seq = i;
        for (auto& pv : entries[i].props)
            entry.props.push_back(std::optional<PropertyValue>(std::move(pv)));
        batch_entries.push_back(std::move(entry));
    }

    co_await inst->async_data->batchInsertEdges(elabel_id, std::move(batch_entries));

    co_return static_cast<int32_t>(count);
}

folly::coro::Task<CypherExecutionContext> GraphService::handleDatabaseDdl(const DatabaseDdlStatement& stmt,
                                                                          IAsyncGraphDataStore& data_store) {
    CypherExecutionContext result;
    auto ctx = std::make_shared<compute::StreamContext>(data_store);
    Schema columns;
    std::vector<Row> rows;

    switch (stmt.type) {
    case DatabaseDdlStatement::USE_GRAPH: {
        result.switched_database = stmt.name;
        spdlog::info("[service] switched to database: {}", stmt.name);
        columns = {"current_database"};
        Row row;
        row.push_back(std::string(stmt.name));
        rows.push_back(std::move(row));
        break;
    }
    case DatabaseDdlStatement::CREATE_DATABASE: {
        auto entry = gm_.createGraph(stmt.name);
        spdlog::info("[service] created database: {}", stmt.name);
        columns = {"result"};
        Row row;
        row.push_back(std::string("Database created: " + stmt.name));
        rows.push_back(std::move(row));
        break;
    }
    case DatabaseDdlStatement::DROP_DATABASE: {
        bool ok = false;
        std::string error_msg;
        try {
            ok = gm_.dropGraph(stmt.name);
        } catch (const std::exception& e) {
            error_msg = e.what();
        }
        spdlog::info("[service] dropped database: {} (success={})", stmt.name, ok);
        columns = {"result"};
        Row row;
        if (ok) {
            row.push_back(std::string("Database dropped: " + stmt.name));
        } else {
            row.push_back(
                std::string("Failed to drop database: " + stmt.name + (error_msg.empty() ? "" : " - " + error_msg)));
        }
        rows.push_back(std::move(row));
        break;
    }
    case DatabaseDdlStatement::SHOW_DATABASES: {
        auto graphs = gm_.listGraphs();
        columns = {"name", "status", "type", "current"};
        for (auto& g : graphs) {
            Row row;
            row.push_back(std::string(g.name));
            row.push_back(std::string("online"));
            row.push_back(std::string("standard"));
            row.push_back(bool(g.name == "default")); // current — tracks the session default
            rows.push_back(std::move(row));
        }
        break;
    }
    case DatabaseDdlStatement::SHOW_DATABASE: {
        auto graphs = gm_.listGraphs();
        columns = {"name", "status", "type", "current"};
        for (auto& g : graphs) {
            if (g.name != stmt.name)
                continue;
            Row row;
            row.push_back(std::string(g.name));
            row.push_back(std::string("online"));
            row.push_back(std::string("standard"));
            row.push_back(bool(false));
            rows.push_back(std::move(row));
            break;
        }
        break;
    }
    }

    ctx->columns = std::move(columns);
    auto ddl_row_gen =
        folly::coro::co_invoke([rows = std::move(rows)]() mutable -> folly::coro::AsyncGenerator<RowBatch> {
            if (!rows.empty()) {
                RowBatch batch;
                batch.rows = std::move(rows);
                co_yield std::move(batch);
            }
        });
    ctx->gen = compute::wrapRowBatchToChunkGenerator(std::move(ddl_row_gen));
    result.ctx = std::move(ctx);
    co_return result;
}

} // namespace service
} // namespace eugraph
