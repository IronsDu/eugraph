#include "service/graph_service.hpp"

#include "query/function/function_registry.hpp"
#include "query/physical_plan/physical_operator_base.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace eugraph {
namespace service {
namespace {

std::string catalogTypeName(const binder::BoundType& type) {
    switch (type.kind) {
    case binder::BoundTypeKind::BOOL:
        return "BOOLEAN";
    case binder::BoundTypeKind::INT64:
        return "INTEGER";
    case binder::BoundTypeKind::DOUBLE:
        return "FLOAT";
    case binder::BoundTypeKind::STRING:
        return "STRING";
    case binder::BoundTypeKind::VERTEX:
    case binder::BoundTypeKind::VERTEX_REF:
        return "NODE";
    case binder::BoundTypeKind::EDGE:
    case binder::BoundTypeKind::EDGE_KEY:
        return "RELATIONSHIP";
    case binder::BoundTypeKind::PATH:
    case binder::BoundTypeKind::PATH_TOPOLOGY:
        return "PATH";
    case binder::BoundTypeKind::DATETIME:
        return "DATE_TIME";
    case binder::BoundTypeKind::TIME:
        return "TIME";
    case binder::BoundTypeKind::DURATION:
        return "DURATION";
    case binder::BoundTypeKind::LIST:
        return "LIST<" + (type.element_type ? catalogTypeName(*type.element_type) : "ANY") + ">";
    case binder::BoundTypeKind::MAP:
        return "MAP";
    case binder::BoundTypeKind::NULL_TYPE:
        return "NULL";
    case binder::BoundTypeKind::ANY:
        return "ANY";
    }
    return "ANY";
}

std::string catalogFunctionSignature(const function::FunctionDef& def) {
    std::string sig = def.name + "(";
    if (def.has_variadic_args) {
        sig += "...";
    } else {
        for (size_t i = 0; i < def.arg_types.size(); ++i) {
            if (i > 0)
                sig += ", ";
            sig += "input" + std::to_string(i) + " :: " + catalogTypeName(def.arg_types[i]);
        }
    }
    sig += ") :: (";
    sig += catalogTypeName(def.return_type);
    sig += ")";
    return sig;
}

ListValue catalogStringList(const std::vector<std::string>& values) {
    ListValue list;
    for (const auto& value : values)
        list.elements.push_back({ValueStorage{Value{value}}});
    return list;
}

struct ProcedureShowEntry {
    const char* name;
    const char* signature;
    const char* description;
};

std::vector<ProcedureShowEntry> builtinProcedureShowEntries() {
    return {
        {"db.ping", "db.ping() :: (success :: BOOLEAN)", "Check whether the database is reachable."},
        {"db.schema.visualization",
         "db.schema.visualization() :: (nodes :: LIST<NODE>, relationships :: LIST<RELATIONSHIP>)",
         "Return the schema as a virtual graph."},
        {"dbms.clientConfig", "dbms.clientConfig() :: (name :: STRING, value :: ANY)",
         "Return Neo4j Browser client configuration entries."},
        {"db.indexes",
         "db.indexes() :: (id :: INTEGER, name :: STRING, state :: STRING, populationPercent :: FLOAT, "
         "uniqueness :: STRING, type :: STRING, entityType :: STRING, labelsOrTypes :: LIST<STRING>, "
         "properties :: LIST<STRING>, owningConstraint :: NULL)",
         "List all indexes."},
        {"dbms.procedures",
         "dbms.procedures() :: (name :: STRING, signature :: STRING, description :: STRING, "
         "mode :: STRING, roles :: LIST<STRING>)",
         "List all procedures."},
        {"dbms.components", "dbms.components() :: (name :: STRING, versions :: LIST<STRING>, edition :: STRING)",
         "List DBMS components."},
        {"dbms.functions", "dbms.functions() :: (name :: STRING, signature :: STRING, description :: STRING)",
         "List all functions."},
        {"dbms.info", "dbms.info() :: (id :: STRING, name :: STRING, creationDate :: STRING)",
         "Return DBMS information."},
        {"db.labels", "db.labels() :: (label :: STRING)", "List all labels."},
        {"db.relationshipTypes", "db.relationshipTypes() :: (relationshipType :: STRING)",
         "List all relationship types."},
        {"db.propertyKeys", "db.propertyKeys() :: (propertyKey :: STRING)", "List all property keys."},
    };
}

} // namespace

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

    // Neo4j Browser commonly uses `neo4j` (the default DB name) or `system`
    // (its administration DB). EuGraph currently exposes one database named
    // `default`, so alias those well-known names for regular Cypher queries.
    std::string resolved_graph = graph_name;
    if (resolved_graph == "neo4j" || resolved_graph == "system")
        resolved_graph = GraphManager::kDefaultGraphName;

    auto* inst = resolveGraph(resolved_graph);

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
        if (stmt.yield_all) {
            // Neo4j Browser 5+ uses `SHOW DATABASES YIELD *` and validates the
            // full SHOW DATABASES record shape.
            columns = {
                "name",          "type",          "aliases", "access",  "address", "role",         "requestedStatus",
                "currentStatus", "statusMessage", "error",   "default", "home",    "constituents", "defaultLanguage",
                "writer"};
            for (auto& g : graphs) {
                bool is_default = g.name == "default";
                Row row;
                row.push_back(std::string(g.name));
                row.push_back(std::string("standard"));
                row.push_back(Value{ListValue{}});
                row.push_back(std::string("READ_WRITE"));
                row.push_back(std::string("localhost:17687"));
                row.push_back(Value{});
                row.push_back(std::string("online"));
                row.push_back(std::string("online"));
                row.push_back(std::string(""));
                row.push_back(std::string(""));
                row.push_back(bool(is_default));
                row.push_back(bool(is_default));
                row.push_back(Value{ListValue{}});
                row.push_back(std::string(""));
                row.push_back(bool(false));
                rows.push_back(std::move(row));
            }
        } else {
            columns = {"name", "status", "type", "current", "currentStatus"};
            for (auto& g : graphs) {
                Row row;
                row.push_back(std::string(g.name));
                row.push_back(std::string("online"));
                row.push_back(std::string("standard"));
                row.push_back(bool(g.name == "default")); // current — tracks the session default
                row.push_back(std::string("online"));
                rows.push_back(std::move(row));
            }
        }
        break;
    }
    case DatabaseDdlStatement::SHOW_DATABASE: {
        auto graphs = gm_.listGraphs();
        columns = {"name", "status", "type", "current", "currentStatus"};
        for (auto& g : graphs) {
            if (g.name != stmt.name)
                continue;
            Row row;
            row.push_back(std::string(g.name));
            row.push_back(std::string("online"));
            row.push_back(std::string("standard"));
            row.push_back(bool(false));
            row.push_back(std::string("online"));
            rows.push_back(std::move(row));
            break;
        }
        break;
    }
    case DatabaseDdlStatement::SHOW_CURRENT_USER: {
        columns = {"user", "roles", "passwordChangeRequired", "suspended", "home"};
        Row row;
        row.push_back(std::string("neo4j"));
        row.push_back(catalogStringList({"PUBLIC"}));
        row.push_back(bool(false));
        row.push_back(bool(false));
        row.push_back(Value{});
        rows.push_back(std::move(row));
        break;
    }
    case DatabaseDdlStatement::SHOW_PROCEDURES: {
        columns = {"name",   "signature",           "description",      "mode", "admin", "worksOnSystem",
                   "option", "argumentDescription", "returnDescription"};
        for (const auto& entry : builtinProcedureShowEntries()) {
            Row row;
            row.push_back(std::string(entry.name));
            row.push_back(std::string(entry.signature));
            row.push_back(std::string(entry.description));
            row.push_back(std::string("READ"));
            row.push_back(bool(false));
            row.push_back(bool(false));
            row.push_back(Value{MapValue{}});
            row.push_back(Value{ListValue{}});
            row.push_back(std::string(""));
            rows.push_back(std::move(row));
        }
        break;
    }
    case DatabaseDdlStatement::SHOW_FUNCTIONS: {
        columns = {"name",     "signature", "description",         "aggregating",
                   "category", "isBuiltIn", "argumentDescription", "returnDescription"};
        function::FunctionRegistry registry;
        auto defs = registry.listFunctions();
        defs.erase(std::remove_if(defs.begin(), defs.end(), [](const auto& def) { return def.name.starts_with("__"); }),
                   defs.end());
        std::sort(defs.begin(), defs.end(), [](const auto& a, const auto& b) {
            if (a.name != b.name)
                return a.name < b.name;
            return catalogFunctionSignature(a) < catalogFunctionSignature(b);
        });
        for (const auto& def : defs) {
            ListValue args;
            for (size_t i = 0; i < def.arg_types.size(); ++i) {
                MapValue arg;
                arg.entries.push_back({"name", ValueStorage{Value{std::string{"input"} + std::to_string(i)}}});
                arg.entries.push_back({"description", ValueStorage{Value{std::string{}}}});
                arg.entries.push_back({"type", ValueStorage{Value{catalogTypeName(def.arg_types[i])}}});
                args.elements.push_back({ValueStorage{Value{arg}}});
            }

            Row row;
            row.push_back(def.name);
            row.push_back(catalogFunctionSignature(def));
            row.push_back(std::string(def.is_aggregate ? "Aggregate function" : "Scalar function"));
            row.push_back(bool(def.is_aggregate));
            row.push_back(std::string(def.is_aggregate ? "Aggregate" : "Scalar"));
            row.push_back(bool(true));
            row.push_back(Value{args});
            row.push_back(catalogTypeName(def.return_type));
            rows.push_back(std::move(row));
        }
        break;
    }
    case DatabaseDdlStatement::SHOW_VECTOR_INDEXES: {
        columns = {"id",         "name",          "state",      "populationPercent", "type",
                   "entityType", "labelsOrTypes", "properties", "indexProvider",     "owningConstraint",
                   "lastRead",   "readCount",     "options"};
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
