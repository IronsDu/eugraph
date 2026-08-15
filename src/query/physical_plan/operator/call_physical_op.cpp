#include "query/physical_plan/operator/call_physical_op.hpp"

#include "common/types/graph_types.hpp"
#include "query/function/function_registry.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

namespace {

constexpr int64_t kVirtualEdgeIdBase = -1000000;

VertexId virtualNodeId(LabelId label_id) {
    // Virtual node ids must not collide with real vertex ids (positive) or
    // with each other. Label ids are small uint16 values, so this range is
    // stable and deterministic for the lifetime of a query.
    return static_cast<VertexId>(-1 - static_cast<int64_t>(label_id));
}

VertexValue buildVirtualNode(LabelId label_id) {
    VertexValue node;
    node.id = virtualNodeId(label_id);
    node.labels = LabelIdSet{label_id};
    return node;
}

EdgeValue buildVirtualRelationship(VertexId src_id, VertexId dst_id, EdgeLabelId edge_label_id, int64_t virtual_id) {
    EdgeValue edge;
    edge.id = static_cast<EdgeId>(virtual_id);
    edge.src_id = src_id;
    edge.dst_id = dst_id;
    edge.label_id = edge_label_id;
    edge.seq = 0;
    edge.properties = Properties{};
    return edge;
}

std::string normalizedProcedureName(std::string name) {
    if (name.size() > 2 && name.substr(name.size() - 2) == "()")
        name.resize(name.size() - 2);
    return name;
}

std::string indexStateName(IndexState state) {
    switch (state) {
    case IndexState::PUBLIC:
        return "ONLINE";
    case IndexState::WRITE_ONLY:
        return "POPULATING";
    case IndexState::DELETE_ONLY:
        return "ONLINE";
    case IndexState::ERROR:
        return "FAILED";
    }
    return "ONLINE";
}

double indexPopulationPercent(IndexState state) {
    switch (state) {
    case IndexState::PUBLIC:
    case IndexState::DELETE_ONLY:
        return 100.0;
    case IndexState::WRITE_ONLY:
    case IndexState::ERROR:
        return 0.0;
    }
    return 0.0;
}

std::string neo4jTypeName(const binder::BoundType& type) {
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
        return "LIST<" + (type.element_type ? neo4jTypeName(*type.element_type) : "ANY") + ">";
    case binder::BoundTypeKind::MAP:
        return "MAP";
    case binder::BoundTypeKind::NULL_TYPE:
        return "NULL";
    case binder::BoundTypeKind::ANY:
        return "ANY";
    }
    return "ANY";
}

std::string functionSignature(const function::FunctionDef& def) {
    std::string sig = def.name + "(";
    if (def.has_variadic_args) {
        sig += "...";
    } else {
        for (size_t i = 0; i < def.arg_types.size(); ++i) {
            if (i > 0)
                sig += ", ";
            sig += "input" + std::to_string(i) + " :: " + neo4jTypeName(def.arg_types[i]);
        }
    }
    sig += ") :: (";
    sig += neo4jTypeName(def.return_type);
    sig += ")";
    return sig;
}

ListValue stringList(const std::vector<std::string>& values) {
    ListValue list;
    list.elements.reserve(values.size());
    for (const auto& value : values)
        list.elements.push_back({ValueStorage{Value{value}}});
    return list;
}

std::vector<std::pair<std::string, Value>> clientConfigRows() {
    return {
        {"browser.allow_outgoing_connections", Value{false}},
        {"browser.credential_timeout", Value{int64_t{0}}},
        {"browser.post_connect_cmd", Value{std::string{}}},
        {"browser.remote_content_hostname_whitelist", Value{std::string{"guides.neo4j.com, localhost"}}},
        {"browser.retain_connection_credentials", Value{false}},
        {"browser.retain_editor_history", Value{true}},
        {"dbms.security.auth_enabled", Value{true}},
        {"clients.allow_telemetry", Value{false}},
        {"metrics.namespaces.enabled", Value{false}},
        {"metrics.prefix", Value{std::string{"neo4j"}}},
    };
}

struct ProcedureEntry {
    const char* name;
    const char* signature;
    const char* description;
    const char* mode;
};

std::vector<ProcedureEntry> builtinProcedures() {
    return {
        {"db.ping", "db.ping() :: (success :: BOOLEAN)", "Check whether the database is reachable.", "READ"},
        {"db.schema.visualization", "db.schema.visualization() :: (nodes :: LIST<MAP>, relationships :: LIST<MAP>)",
         "Describe the graph schema for visualization tools.", "READ"},
        {"dbms.clientConfig", "dbms.clientConfig() :: (name :: STRING, value :: ANY)",
         "Return client configuration settings for Neo4j Browser.", "READ"},
        {"db.indexes",
         "db.indexes() :: (id :: INTEGER, name :: STRING, state :: STRING, populationPercent :: FLOAT, "
         "uniqueness :: STRING, type :: STRING, entityType :: STRING, labelsOrTypes :: LIST<STRING>, "
         "properties :: LIST<STRING>, owningConstraint :: NULL)",
         "List all indexes in the current database.", "READ"},
        {"dbms.procedures",
         "dbms.procedures() :: (name :: STRING, signature :: STRING, description :: STRING, "
         "mode :: STRING, roles :: LIST<STRING>)",
         "List all procedures available in the current database.", "READ"},
        {"dbms.components", "dbms.components() :: (name :: STRING, versions :: LIST<STRING>, edition :: STRING)",
         "List DBMS components and their versions.", "READ"},
        {"dbms.functions", "dbms.functions() :: (name :: STRING, signature :: STRING, description :: STRING)",
         "List all functions available in the current database.", "READ"},
        {"dbms.info", "dbms.info() :: (id :: STRING, name :: STRING, creationDate :: STRING)",
         "Return DBMS information.", "READ"},
        {"db.labels", "db.labels() :: (label :: STRING)", "List all labels in the current database.", "READ"},
        {"db.relationshipTypes", "db.relationshipTypes() :: (relationshipType :: STRING)",
         "List all relationship types in the current database.", "READ"},
        {"db.propertyKeys", "db.propertyKeys() :: (propertyKey :: STRING)",
         "List all property keys in the current database.", "READ"},
    };
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> CallPhysicalOp::executeChunk() {
    const std::string procedure = normalizedProcedureName(procedure_name_);
    spdlog::info("[CallPhysicalOp] executeChunk called, procedure={}", procedure);

    std::vector<std::vector<Value>> rows;

    if (procedure == "db.ping") {
        rows.push_back({Value{true}});
    } else if (procedure == "dbms.components") {
        // Neo4j Browser derives the server version from the `Neo4j Kernel`
        // component and the Cypher version from the `Cypher` component. Keep
        // the advertised server version in the 4.4.x range so Browser uses
        // CALL dbms.procedures()/dbms.functions() (both are implemented).
        ListValue kernel_versions;
        kernel_versions.elements.push_back({ValueStorage{Value{std::string{"4.4.3"}}}});
        rows.push_back({Value{std::string{"Neo4j Kernel"}}, Value{kernel_versions}, Value{std::string{"community"}}});

        ListValue cypher_versions;
        cypher_versions.elements.push_back({ValueStorage{Value{std::string{"5"}}}});
        rows.push_back({Value{std::string{"Cypher"}}, Value{cypher_versions}, Value{std::string{"community"}}});
    } else if (procedure == "dbms.clientConfig") {
        for (const auto& [name, value] : clientConfigRows())
            rows.push_back({Value{name}, value});
    } else if (procedure == "db.indexes") {
        if (meta_) {
            auto indexes = co_await meta_->listIndexes();
            std::sort(indexes.begin(), indexes.end(), [](const auto& a, const auto& b) {
                if (a.is_edge != b.is_edge)
                    return !a.is_edge;
                if (a.label_name != b.label_name)
                    return a.label_name < b.label_name;
                return a.name < b.name;
            });
            for (size_t i = 0; i < indexes.size(); ++i) {
                const auto& idx = indexes[i];
                rows.push_back({Value{static_cast<int64_t>(i + 1)}, Value{idx.name}, Value{indexStateName(idx.state)},
                                Value{indexPopulationPercent(idx.state)},
                                Value{std::string{idx.unique ? "UNIQUE" : "NONUNIQUE"}}, Value{std::string{"BTREE"}},
                                Value{std::string{idx.is_edge ? "RELATIONSHIP" : "NODE"}},
                                Value{stringList({idx.label_name})}, Value{stringList(idx.property_names)}, Value{}});
            }
        }
    } else if (procedure == "dbms.procedures") {
        for (const auto& entry : builtinProcedures()) {
            rows.push_back({Value{std::string{entry.name}}, Value{std::string{entry.signature}},
                            Value{std::string{entry.description}}, Value{std::string{entry.mode}},
                            Value{stringList({"PUBLIC"})}});
        }
    } else if (procedure == "dbms.info") {
        rows.push_back({Value{std::string{"eugraph-1"}}, Value{std::string{"EuGraph"}},
                        Value{std::string{"2026-08-15T00:00:00Z"}}});
    } else if (procedure == "dbms.functions") {
        std::vector<function::FunctionDef> defs;
        if (func_registry_) {
            defs = func_registry_->listFunctions();
            defs.erase(
                std::remove_if(defs.begin(), defs.end(), [](const auto& def) { return def.name.starts_with("__"); }),
                defs.end());
            std::sort(defs.begin(), defs.end(), [](const auto& a, const auto& b) {
                if (a.name != b.name)
                    return a.name < b.name;
                return functionSignature(a) < functionSignature(b);
            });
        }
        for (const auto& def : defs) {
            rows.push_back({Value{def.name}, Value{functionSignature(def)},
                            Value{std::string{def.is_aggregate ? "Aggregate function" : "Scalar function"}}});
        }
    } else if (procedure == "db.labels") {
        if (meta_) {
            auto labels = co_await meta_->listLabels();
            std::vector<std::string> names;
            for (const auto& label : labels) {
                if (label.name != kAnonLabelName)
                    names.push_back(label.name);
            }
            std::sort(names.begin(), names.end());
            for (const auto& name : names)
                rows.push_back({Value{name}});
        }
    } else if (procedure == "db.relationshipTypes") {
        if (meta_) {
            auto edge_labels = co_await meta_->listEdgeLabels();
            std::vector<std::string> names;
            for (const auto& edge_label : edge_labels)
                names.push_back(edge_label.name);
            std::sort(names.begin(), names.end());
            for (const auto& name : names)
                rows.push_back({Value{name}});
        }
    } else if (procedure == "db.propertyKeys") {
        if (meta_) {
            std::set<std::string> keys;
            auto labels = co_await meta_->listLabels();
            for (const auto& label : labels) {
                for (const auto& prop : label.properties)
                    keys.insert(prop.name);
            }
            auto edge_labels = co_await meta_->listEdgeLabels();
            for (const auto& edge_label : edge_labels) {
                for (const auto& prop : edge_label.properties)
                    keys.insert(prop.name);
            }
            for (const auto& key : keys)
                rows.push_back({Value{key}});
        }
    } else if (procedure == "db.schema.visualization") {
        spdlog::info("[CallPhysicalOp] db.schema.visualization path, meta_={} data_store_={}", (void*)meta_,
                     (void*)data_store_);
        ListValue nodes;
        ListValue rels;

        if (meta_ && data_store_) {
            // Labels in use: only labels that actually have vertices become
            // virtual nodes, matching Neo4j's db.schema.visualization().
            auto labels = co_await meta_->listLabels();
            std::vector<LabelDef> labels_in_use;
            for (const auto& ldef : labels) {
                if (ldef.name == kAnonLabelName)
                    continue;
                auto scan = data_store_->scanVerticesByLabel(ldef.id);
                while (auto batch = co_await scan.next()) {
                    if (batch && !batch->empty()) {
                        labels_in_use.push_back(ldef);
                        break;
                    }
                }
            }
            std::sort(labels_in_use.begin(), labels_in_use.end(),
                      [](const auto& a, const auto& b) { return a.name < b.name; });

            std::unordered_map<LabelId, VertexId> virtual_ids;
            for (const auto& ldef : labels_in_use) {
                virtual_ids[ldef.id] = virtualNodeId(ldef.id);
                nodes.elements.push_back({ValueStorage{Value{buildVirtualNode(ldef.id)}}});
            }

            // Relationship types in use: scan edges of each type, collect the
            // endpoint labels, then emit the same cross-product of start/end
            // label nodes that Neo4j builds for the schema meta-graph.
            auto edge_labels = co_await meta_->listEdgeLabels();
            std::sort(edge_labels.begin(), edge_labels.end(),
                      [](const auto& a, const auto& b) { return a.name < b.name; });
            int64_t next_virtual_edge_id = kVirtualEdgeIdBase;
            for (const auto& eldef : edge_labels) {
                std::set<LabelId> start_labels;
                std::set<LabelId> end_labels;

                auto edges = data_store_->scanEdgesByType(eldef.id, std::nullopt, std::nullopt);
                while (auto batch = co_await edges.next()) {
                    if (!batch)
                        continue;
                    for (const auto& edge : *batch) {
                        auto src_labels = co_await data_store_->getVertexLabels(edge.src_vertex_id);
                        for (LabelId lid : src_labels) {
                            if (virtual_ids.count(lid))
                                start_labels.insert(lid);
                        }
                        auto dst_labels = co_await data_store_->getVertexLabels(edge.dst_vertex_id);
                        for (LabelId lid : dst_labels) {
                            if (virtual_ids.count(lid))
                                end_labels.insert(lid);
                        }
                    }
                }

                for (LabelId start_label : start_labels) {
                    for (LabelId end_label : end_labels) {
                        rels.elements.push_back({ValueStorage{Value{buildVirtualRelationship(
                            virtual_ids[start_label], virtual_ids[end_label], eldef.id, next_virtual_edge_id--)}}});
                    }
                }
            }
        }

        spdlog::info("[CallPhysicalOp] built schema: {} nodes, {} rels", nodes.elements.size(), rels.elements.size());
        rows.push_back({Value{std::move(nodes)}, Value{std::move(rels)}});
    }

    if (rows.empty()) {
        spdlog::info("[CallPhysicalOp] yielding no rows for procedure={}", procedure);
        co_return;
    }

    DataChunk output;
    output.setSchema(output_types_);
    output.reserve(rows.size());
    for (auto& row : rows)
        output.appendRow(row);
    output.sel = SelectionVector::identity(output.count);

    spdlog::info("[CallPhysicalOp] yielding {} row(s) for procedure={}", output.count, procedure);
    co_yield output;
}

} // namespace compute
} // namespace eugraph
