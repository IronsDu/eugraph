#include "service/graph_service.hpp"

#include "common/types/query_error.hpp"
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

/// Schema property type -> the name used in catalog output.
///
/// Deliberately mirrors the mapping in CallPhysicalOp (same strings), so
/// `DESCRIBE LABEL x` and `CALL db.schema.nodeTypeProperties()` report the same
/// type name for the same field. Keep the two in sync.
std::string propertyTypeName(PropertyType type) {
    switch (type) {
    case PropertyType::BOOL:
        return "BOOLEAN";
    case PropertyType::INT64:
        return "INTEGER";
    case PropertyType::DOUBLE:
        return "FLOAT";
    case PropertyType::STRING:
        return "STRING";
    case PropertyType::INT64_ARRAY:
        return "INTEGER_ARRAY";
    case PropertyType::DOUBLE_ARRAY:
        return "FLOAT_ARRAY";
    case PropertyType::STRING_ARRAY:
        return "STRING_ARRAY";
    case PropertyType::DATETIME:
        return "DATE_TIME";
    case PropertyType::TIME:
        return "TIME";
    case PropertyType::DURATION:
        return "DURATION";
    case PropertyType::DATETIME_ARRAY:
        return "DATE_TIME_ARRAY";
    case PropertyType::TIME_ARRAY:
        return "TIME_ARRAY";
    case PropertyType::DURATION_ARRAY:
        return "DURATION_ARRAY";
    case PropertyType::BYTES:
        return "BYTE_ARRAY";
    case PropertyType::ANY:
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
        {"db.schema.nodeTypeProperties",
         "db.schema.nodeTypeProperties() :: (nodeLabels :: LIST<STRING>, propertyName :: STRING, "
         "propertyTypes :: LIST<STRING>)",
         "List node labels and their property types."},
        {"db.schema.relTypeProperties",
         "db.schema.relTypeProperties() :: (relType :: STRING, propertyName :: STRING, "
         "propertyTypes :: LIST<STRING>)",
         "List relationship types and their property types."},
    };
}

} // namespace

folly::Executor* GraphService::computeExecutor() {
    auto* inst = gm_.getGraph(GraphManager::kDefaultGraphName);
    return inst && inst->executor ? inst->executor->computeExecutor() : nullptr;
}

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
                            const std::string& graph_name, compute::QueryCancel cancel) {
    // neo4j Browser commonly uses the default DB names `neo4j` / `system`; alias them
    // onto our single default graph. The alias applies to DDL interception too, so it
    // has to be resolved before the graph-scoped DESCRIBE family picks an instance.
    std::string resolved_graph = graph_name;
    if (resolved_graph == "neo4j" || resolved_graph == "system")
        resolved_graph = GraphManager::kDefaultGraphName;

    // Database DDL is intercepted before the normal Cypher pipeline.
    //
    // The instance passed here is the *selected* graph, which is what the DESCRIBE
    // family needs. Database-level statements (CREATE/DROP/SHOW DATABASE, USE) do not
    // read graph schema and resolve the default graph themselves inside the handler.
    // When the requested graph does not exist (harmless for database-level DDL, e.g.
    // `DROP DATABASE x` while x is already gone) we fall back to the default instance
    // instead of failing the statement.
    auto ddl_stmt = DatabaseDdlParser::tryParse(query);
    if (ddl_stmt.has_value()) {
        auto* ddl_inst = gm_.getGraph(resolved_graph);
        if (!ddl_inst)
            ddl_inst = resolveGraph(GraphManager::kDefaultGraphName);
        co_return co_await handleDatabaseDdl(*ddl_stmt, *ddl_inst);
    }

    auto* inst = resolveGraph(resolved_graph);

    auto ctx = co_await inst->executor->prepareStream(query, params, std::move(cancel));

    if (!ctx->error.empty()) {
        // 绑定/解析错误的原因分类写在消息里（"Binding failed; SyntaxError: ..."），
        // 在这里统一翻译成 Neo4j 状态码，而不是让每个前端各自猜。
        throw QueryException(classifyQueryErrorMessage(ctx->error), ctx->error);
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
        entry.label_props.emplace_back(label_id, Properties{});
        auto& props = entry.label_props.back().second;
        for (auto& pv : entries[i].props)
            props.push_back(std::optional<PropertyValue>(std::move(pv)));

        for (const auto& extra_label : entries[i].extra_labels) {
            if (extra_label == label_name)
                continue;
            auto extra_id_opt = co_await inst->async_meta->getLabelId(extra_label);
            if (!extra_id_opt.has_value()) {
                throw std::runtime_error("Label not found: " + extra_label);
            }
            entry.label_props.emplace_back(*extra_id_opt, Properties{});
        }
        batch_entries.push_back(std::move(entry));
    }

    co_await inst->async_data->batchInsertVertices(std::move(batch_entries));

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
                                                                          GraphInstance& instance) {
    CypherExecutionContext result;
    auto ctx = std::make_shared<compute::StreamContext>(*instance.async_data);
    Schema columns;
    std::vector<Row> rows;

    // Database-level statements below are answered from the default graph regardless
    // of which graph the session selected, so they keep resolving it themselves. The
    // DESCRIBE family is graph-scoped and reads `instance.async_meta` directly.
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
                row.push_back(Value(mk<ListValue>()));
                row.push_back(std::string("READ_WRITE"));
                row.push_back(std::string("localhost:17687"));
                row.push_back(Value{});
                row.push_back(std::string("online"));
                row.push_back(std::string("online"));
                row.push_back(std::string(""));
                row.push_back(std::string(""));
                row.push_back(bool(is_default));
                row.push_back(bool(is_default));
                row.push_back(Value(mk<ListValue>()));
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
        row.push_back(Value(mk<ListValue>(catalogStringList({"PUBLIC"}))));
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
            row.push_back(Value(mk<MapValue>()));
            row.push_back(Value(mk<ListValue>()));
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
                args.elements.push_back({ValueStorage{Value(mk<MapValue>(std::move(arg)))}});
            }

            Row row;
            row.push_back(def.name);
            row.push_back(catalogFunctionSignature(def));
            row.push_back(std::string(def.is_aggregate ? "Aggregate function" : "Scalar function"));
            row.push_back(bool(def.is_aggregate));
            row.push_back(std::string(def.is_aggregate ? "Aggregate" : "Scalar"));
            row.push_back(bool(true));
            row.push_back(Value(mk<ListValue>(std::move(args))));
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
    // ── DESCRIBE family: graph-scoped schema introspection ──
    //
    // Unlike the SHOW statements above, these read the *selected* graph and they do
    // NOT filter out the anonymous label: seeing the fields of unlabeled nodes is the
    // point. `anonymous` distinguishes it, so the internal name stays legible.
    // `CALL db.labels()` keeps filtering it out -- a documented divergence.
    case DatabaseDdlStatement::DESCRIBE_LABELS: {
        columns = {"name", "anonymous"};
        auto labels = co_await instance.async_meta->listLabels();
        std::sort(labels.begin(), labels.end(), [](const LabelDef& a, const LabelDef& b) { return a.name < b.name; });
        for (const auto& label : labels) {
            Row row;
            row.push_back(std::string(label.name));
            row.push_back(bool(label.name == kAnonLabelName));
            rows.push_back(std::move(row));
        }
        break;
    }
    case DatabaseDdlStatement::DESCRIBE_RELATIONSHIPS: {
        columns = {"relationshipType"};
        auto edge_labels = co_await instance.async_meta->listEdgeLabels();
        std::vector<std::string> names;
        names.reserve(edge_labels.size());
        for (const auto& edge_label : edge_labels)
            names.push_back(edge_label.name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            Row row;
            row.push_back(name);
            rows.push_back(std::move(row));
        }
        break;
    }
    case DatabaseDdlStatement::DESCRIBE_LABEL: {
        // `propertyType` is singular and a plain STRING: a declared field has exactly
        // one PropertyType. (The procedures keep the plural LIST form because that is
        // neo4j's shape; DESCRIBE is our own surface and has no such constraint.)
        columns = {"label", "propertyName", "propertyType"};
        auto labels = co_await instance.async_meta->listLabels();
        for (auto& label : labels) {
            if (label.name != stmt.name)
                continue;
            std::sort(label.properties.begin(), label.properties.end(),
                      [](const PropertyDef& a, const PropertyDef& b) { return a.name < b.name; });
            for (const auto& prop : label.properties) {
                Row row;
                row.push_back(std::string(label.name));
                row.push_back(prop.name);
                row.push_back(propertyTypeName(prop.type));
                rows.push_back(std::move(row));
            }
            break; // names are unique; no second label can match
        }
        break;
    }
    case DatabaseDdlStatement::DESCRIBE_RELATIONSHIP: {
        columns = {"relType", "propertyName", "propertyType"};
        auto edge_labels = co_await instance.async_meta->listEdgeLabels();
        for (auto& edge_label : edge_labels) {
            if (edge_label.name != stmt.name)
                continue;
            std::sort(edge_label.properties.begin(), edge_label.properties.end(),
                      [](const PropertyDef& a, const PropertyDef& b) { return a.name < b.name; });
            for (const auto& prop : edge_label.properties) {
                Row row;
                row.push_back(std::string(edge_label.name));
                row.push_back(prop.name);
                row.push_back(propertyTypeName(prop.type));
                rows.push_back(std::move(row));
            }
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
