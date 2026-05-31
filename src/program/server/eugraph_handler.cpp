#include "program/server/eugraph_handler.hpp"

#include "common/types/temporal_value.hpp"
#include "query/parser/cypher_parser.hpp"

#include <thrift/lib/cpp2/async/ServerStream.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>

#include <folly/io/async/EventBaseManager.h>

namespace {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

folly::coro::AsyncGenerator<eugraph::thrift::ResultRowBatch&&>
makeStreamGenerator(std::shared_ptr<eugraph::compute::StreamContext> ctx,
                    std::unordered_map<eugraph::LabelId, eugraph::LabelDef> label_defs,
                    std::unordered_map<eugraph::EdgeLabelId, eugraph::EdgeLabelDef> edge_label_defs,
                    eugraph::server::EuGraphHandler& handler, int64_t t0) {
    size_t total_rows = 0;
    while (auto chunk = co_await ctx->gen.next()) {
        eugraph::thrift::ResultRowBatch thrift_batch;
        auto rows = chunk->toRows();
        for (auto& row : rows) {
            eugraph::thrift::ResultRow row_resp;
            for (auto& val : row) {
                row_resp.values()->push_back(handler.valueToThrift(val, label_defs, edge_label_defs));
            }
            thrift_batch.rows()->push_back(std::move(row_resp));
        }
        total_rows += rows.size();
        co_yield std::move(thrift_batch);
    }
    if (ctx->should_commit) {
        co_await ctx->store.commitTran(ctx->txn);
    }
    spdlog::info("[handler] executeCypher stream done, {} rows, took={}ms", total_rows, nowMs() - t0);
}
} // namespace

namespace eugraph {
namespace server {

// ==================== Helpers ====================

::eugraph::PropertyType EuGraphHandler::toPropertyType(thrift::PropertyType t) {
    switch (t) {
    case thrift::PropertyType::BOOL:
        return ::eugraph::PropertyType::BOOL;
    case thrift::PropertyType::INT64:
        return ::eugraph::PropertyType::INT64;
    case thrift::PropertyType::DOUBLE:
        return ::eugraph::PropertyType::DOUBLE;
    case thrift::PropertyType::STRING:
        return ::eugraph::PropertyType::STRING;
    case thrift::PropertyType::INT64_ARRAY:
        return ::eugraph::PropertyType::INT64_ARRAY;
    case thrift::PropertyType::DOUBLE_ARRAY:
        return ::eugraph::PropertyType::DOUBLE_ARRAY;
    case thrift::PropertyType::STRING_ARRAY:
        return ::eugraph::PropertyType::STRING_ARRAY;
    case thrift::PropertyType::DATETIME:
        return ::eugraph::PropertyType::DATETIME;
    case thrift::PropertyType::TIME:
        return ::eugraph::PropertyType::TIME;
    case thrift::PropertyType::DURATION:
        return ::eugraph::PropertyType::DURATION;
    case thrift::PropertyType::DATETIME_ARRAY:
        return ::eugraph::PropertyType::DATETIME_ARRAY;
    case thrift::PropertyType::TIME_ARRAY:
        return ::eugraph::PropertyType::TIME_ARRAY;
    case thrift::PropertyType::DURATION_ARRAY:
        return ::eugraph::PropertyType::DURATION_ARRAY;
    }
    return ::eugraph::PropertyType::STRING;
}

thrift::PropertyType EuGraphHandler::fromPropertyType(::eugraph::PropertyType t) {
    switch (t) {
    case ::eugraph::PropertyType::ANY:
        return thrift::PropertyType::STRING;
    case ::eugraph::PropertyType::DATETIME:
        return thrift::PropertyType::DATETIME;
    case ::eugraph::PropertyType::TIME:
        return thrift::PropertyType::TIME;
    case ::eugraph::PropertyType::DURATION:
        return thrift::PropertyType::DURATION;
    case ::eugraph::PropertyType::BOOL:
        return thrift::PropertyType::BOOL;
    case ::eugraph::PropertyType::INT64:
        return thrift::PropertyType::INT64;
    case ::eugraph::PropertyType::DOUBLE:
        return thrift::PropertyType::DOUBLE;
    case ::eugraph::PropertyType::STRING:
        return thrift::PropertyType::STRING;
    case ::eugraph::PropertyType::INT64_ARRAY:
        return thrift::PropertyType::INT64_ARRAY;
    case ::eugraph::PropertyType::DOUBLE_ARRAY:
        return thrift::PropertyType::DOUBLE_ARRAY;
    case ::eugraph::PropertyType::STRING_ARRAY:
        return thrift::PropertyType::STRING_ARRAY;
    case ::eugraph::PropertyType::DATETIME_ARRAY:
        return thrift::PropertyType::DATETIME_ARRAY;
    case ::eugraph::PropertyType::TIME_ARRAY:
        return thrift::PropertyType::TIME_ARRAY;
    case ::eugraph::PropertyType::DURATION_ARRAY:
        return thrift::PropertyType::DURATION_ARRAY;
    }
    return thrift::PropertyType::STRING;
}

PropertyDef EuGraphHandler::toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id) {
    PropertyDef def;
    def.id = id;
    def.name = req.name().value();
    def.type = toPropertyType(req.type().value());
    def.required = req.is_required().value();
    return def;
}

GraphInstance* EuGraphHandler::resolveGraph(const std::string& graph_name) {
    auto* inst = graph_manager_.getGraph(graph_name);
    if (!inst)
        throw std::runtime_error("Graph not found: " + graph_name);
    return inst;
}

// ==================== Graph Management ====================

folly::coro::Task<std::unique_ptr<thrift::GraphInfo>>
EuGraphHandler::co_createGraph(std::unique_ptr<std::string> name) {
    spdlog::info("[handler] createGraph '{}'", *name);
    auto entry = graph_manager_.createGraph(*name);
    auto resp = std::make_unique<thrift::GraphInfo>();
    resp->graph_id() = static_cast<int32_t>(entry.graph_id);
    resp->name() = std::move(*name);
    resp->created_at() = static_cast<int64_t>(entry.created_at);
    co_return resp;
}

folly::coro::Task<bool> EuGraphHandler::co_dropGraph(std::unique_ptr<std::string> name) {
    spdlog::info("[handler] dropGraph '{}'", *name);
    co_return graph_manager_.dropGraph(*name);
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::GraphInfo>>> EuGraphHandler::co_listGraphs() {
    spdlog::info("[handler] listGraphs");
    auto entries = graph_manager_.listGraphs();
    auto resp = std::make_unique<std::vector<thrift::GraphInfo>>();
    for (const auto& e : entries) {
        thrift::GraphInfo info;
        info.graph_id() = static_cast<int32_t>(e.graph_id);
        info.name() = e.name;
        info.created_at() = static_cast<int64_t>(e.created_at);
        resp->push_back(std::move(info));
    }
    co_return resp;
}

// ==================== Value conversion ====================

namespace {

void appendJsonValue(std::ostringstream& oss, const PropertyValue& pv) {
    if (std::holds_alternative<bool>(pv)) {
        oss << (std::get<bool>(pv) ? "true" : "false");
    } else if (std::holds_alternative<int64_t>(pv)) {
        oss << std::get<int64_t>(pv);
    } else if (std::holds_alternative<double>(pv)) {
        oss << std::get<double>(pv);
    } else if (std::holds_alternative<std::string>(pv)) {
        oss << '"';
        for (char c : std::get<std::string>(pv)) {
            switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << c;
                break;
            }
        }
        oss << '"';
    } else if (std::holds_alternative<std::vector<int64_t>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<int64_t>>(pv)) {
            if (!first)
                oss << ',';
            oss << x;
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<double>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<double>>(pv)) {
            if (!first)
                oss << ',';
            oss << x;
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<std::string>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& s : std::get<std::vector<std::string>>(pv)) {
            if (!first)
                oss << ',';
            oss << '"' << s << '"';
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<DateTimeValue>(pv)) {
        oss << '"' << temporalToString(std::get<DateTimeValue>(pv)) << '"';
    } else if (std::holds_alternative<TimeValue>(pv)) {
        oss << '"' << temporalToString(std::get<TimeValue>(pv)) << '"';
    } else if (std::holds_alternative<DurationValue>(pv)) {
        oss << '"' << temporalToString(std::get<DurationValue>(pv)) << '"';
    } else if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<DateTimeValue>>(pv)) {
            if (!first)
                oss << ',';
            oss << '"' << temporalToString(x) << '"';
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<TimeValue>>(pv)) {
            if (!first)
                oss << ',';
            oss << '"' << temporalToString(x) << '"';
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<DurationValue>>(pv)) {
            if (!first)
                oss << ',';
            oss << '"' << temporalToString(x) << '"';
            first = false;
        }
        oss << ']';
    } else {
        oss << "null";
    }
}

} // anonymous namespace

thrift::ResultValue
EuGraphHandler::valueToThrift(const Value& val, const std::unordered_map<LabelId, LabelDef>& label_defs,
                              const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs) {
    thrift::ResultValue rv;

    if (std::holds_alternative<std::monostate>(val)) {
        // Leave as __EMPTY__
    } else if (std::holds_alternative<bool>(val)) {
        rv.set_bool_val(std::get<bool>(val));
    } else if (std::holds_alternative<int64_t>(val)) {
        rv.set_int_val(std::get<int64_t>(val));
    } else if (std::holds_alternative<double>(val)) {
        rv.set_double_val(std::get<double>(val));
    } else if (std::holds_alternative<std::string>(val)) {
        rv.set_string_val(std::get<std::string>(val));
    } else if (std::holds_alternative<VertexValue>(val)) {
        auto& v = std::get<VertexValue>(val);
        std::ostringstream oss;
        oss << "{\"id\":" << v.id;

        if (v.labels.has_value() && !v.labels->empty()) {
            // Output all labels for multi-label node support
            oss << ",\"labels\":[";
            bool first = true;
            for (LabelId lid : *v.labels) {
                auto it = label_defs.find(lid);
                if (it == label_defs.end())
                    continue;
                if (!first)
                    oss << ',';
                oss << '"' << it->second.name << '"';
                first = false;
            }
            oss << ']';
            for (LabelId lid : *v.labels) {
                auto it = label_defs.find(lid);
                if (it == label_defs.end())
                    continue;
                auto prop_it = v.properties.find(lid);
                if (prop_it == v.properties.end())
                    continue;
                const auto& props = prop_it->second;
                for (const auto& pd : it->second.properties) {
                    if (pd.id < props.size()) {
                        const auto& pv = props[pd.id];
                        if (pv.has_value()) {
                            oss << ",\"" << pd.name << "\":";
                            appendJsonValue(oss, *pv);
                        }
                    }
                }
            }
        }
        oss << '}';
        rv.set_vertex_json(oss.str());
    } else if (std::holds_alternative<EdgeValue>(val)) {
        auto& e = std::get<EdgeValue>(val);
        std::ostringstream oss;
        oss << "{\"id\":" << e.id << ",\"src\":" << e.src_id << ",\"dst\":" << e.dst_id;

        auto elit = edge_label_defs.find(e.label_id);
        if (elit != edge_label_defs.end()) {
            oss << ",\"label\":\"" << elit->second.name << '"';
        }

        if (e.properties.has_value() && elit != edge_label_defs.end()) {
            for (const auto& pd : elit->second.properties) {
                if (pd.id < e.properties->size()) {
                    const auto& pv = (*e.properties)[pd.id];
                    if (pv.has_value()) {
                        oss << ",\"" << pd.name << "\":";
                        appendJsonValue(oss, *pv);
                    }
                }
            }
        }
        oss << '}';
        rv.set_edge_json(oss.str());
    } else if (std::holds_alternative<PathValue>(val)) {
        auto& p = std::get<PathValue>(val);
        std::ostringstream oss;
        for (size_t i = 0; i < p.elements.size(); ++i) {
            const auto& elem = p.elements[i].value;
            if (std::holds_alternative<VertexValue>(elem)) {
                auto& v = std::get<VertexValue>(elem);
                oss << "(";
                if (v.labels.has_value() && !v.labels->empty()) {
                    for (LabelId lid : *v.labels) {
                        auto it = label_defs.find(lid);
                        if (it != label_defs.end()) {
                            oss << ":" << it->second.name;
                            break;
                        }
                    }
                }
                bool first_prop = true;
                if (v.labels.has_value()) {
                    for (LabelId lid : *v.labels) {
                        auto it = label_defs.find(lid);
                        if (it == label_defs.end())
                            continue;
                        auto prop_it = v.properties.find(lid);
                        if (prop_it == v.properties.end())
                            continue;
                        for (const auto& pd : it->second.properties) {
                            if (pd.id < prop_it->second.size()) {
                                const auto& pv = prop_it->second[pd.id];
                                if (pv.has_value()) {
                                    if (first_prop) {
                                        oss << " {";
                                        first_prop = false;
                                    } else {
                                        oss << ", ";
                                    }
                                    oss << pd.name << ": ";
                                    appendJsonValue(oss, *pv);
                                }
                            }
                        }
                    }
                }
                if (!first_prop)
                    oss << "}";
                oss << ")";
            } else if (std::holds_alternative<EdgeValue>(elem)) {
                auto& e = std::get<EdgeValue>(elem);
                bool outgoing = true;
                if (i > 0 && i + 1 < p.elements.size()) {
                    const auto& prev_v = p.elements[i - 1].value;
                    const auto& next_v = p.elements[i + 1].value;
                    if (std::holds_alternative<VertexValue>(prev_v) && std::holds_alternative<VertexValue>(next_v)) {
                        auto prev_id = std::get<VertexValue>(prev_v).id;
                        auto next_id = std::get<VertexValue>(next_v).id;
                        outgoing = (e.src_id == prev_id && e.dst_id == next_id);
                    }
                }
                oss << (outgoing ? "-[" : "<-[");
                auto elit = edge_label_defs.find(e.label_id);
                if (elit != edge_label_defs.end()) {
                    oss << ":" << elit->second.name;
                }
                oss << (outgoing ? "]->" : "]-");
            }
        }
        rv.set_path_json(oss.str());
    } else if (std::holds_alternative<DateTimeValue>(val)) {
        rv.set_string_val(temporalToString(std::get<DateTimeValue>(val)));
    } else if (std::holds_alternative<TimeValue>(val)) {
        rv.set_string_val(temporalToString(std::get<TimeValue>(val)));
    } else if (std::holds_alternative<DurationValue>(val)) {
        rv.set_string_val(temporalToString(std::get<DurationValue>(val)));
    } else if (std::holds_alternative<ListValue>(val)) {
        auto& lv = std::get<ListValue>(val);
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < lv.elements.size(); ++i) {
            if (i > 0)
                oss << ", ";
            auto elem_rv = valueToThrift(lv.elements[i].value, label_defs, edge_label_defs);
            switch (elem_rv.getType()) {
            case thrift::ResultValue::Type::__EMPTY__:
                oss << "null";
                break;
            case thrift::ResultValue::Type::bool_val:
                oss << (elem_rv.get_bool_val() ? "true" : "false");
                break;
            case thrift::ResultValue::Type::int_val:
                oss << elem_rv.get_int_val();
                break;
            case thrift::ResultValue::Type::double_val:
                oss << elem_rv.get_double_val();
                break;
            case thrift::ResultValue::Type::string_val:
                oss << '"' << elem_rv.get_string_val() << '"';
                break;
            case thrift::ResultValue::Type::vertex_json:
                oss << elem_rv.get_vertex_json();
                break;
            case thrift::ResultValue::Type::edge_json:
                oss << elem_rv.get_edge_json();
                break;
            case thrift::ResultValue::Type::path_json:
                oss << elem_rv.get_path_json();
                break;
            case thrift::ResultValue::Type::list_json:
                oss << elem_rv.get_list_json();
                break;
            case thrift::ResultValue::Type::map_json:
                oss << elem_rv.get_map_json();
                break;
            }
        }
        oss << "]";
        rv.set_list_json(oss.str());
    } else if (std::holds_alternative<MapValue>(val)) {
        auto& mv = std::get<MapValue>(val);
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < mv.entries.size(); ++i) {
            if (i > 0)
                oss << ", ";
            oss << '"' << mv.entries[i].first << "\": ";
            auto elem_rv = valueToThrift(mv.entries[i].second.value, label_defs, edge_label_defs);
            switch (elem_rv.getType()) {
            case thrift::ResultValue::Type::__EMPTY__:
                oss << "null";
                break;
            case thrift::ResultValue::Type::bool_val:
                oss << (elem_rv.get_bool_val() ? "true" : "false");
                break;
            case thrift::ResultValue::Type::int_val:
                oss << elem_rv.get_int_val();
                break;
            case thrift::ResultValue::Type::double_val:
                oss << elem_rv.get_double_val();
                break;
            case thrift::ResultValue::Type::string_val:
                oss << '"' << elem_rv.get_string_val() << '"';
                break;
            case thrift::ResultValue::Type::vertex_json:
                oss << elem_rv.get_vertex_json();
                break;
            case thrift::ResultValue::Type::edge_json:
                oss << elem_rv.get_edge_json();
                break;
            case thrift::ResultValue::Type::path_json:
                oss << elem_rv.get_path_json();
                break;
            case thrift::ResultValue::Type::list_json:
                oss << elem_rv.get_list_json();
                break;
            case thrift::ResultValue::Type::map_json:
                oss << elem_rv.get_map_json();
                break;
            }
        }
        oss << "}";
        rv.set_map_json(oss.str());
    }

    return rv;
}

// ==================== DDL: Label ====================

folly::coro::Task<std::unique_ptr<thrift::LabelInfo>>
EuGraphHandler::co_createLabel(std::unique_ptr<std::string> name,
                               std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties,
                               std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    spdlog::info("[handler] createLabel start, graph='{}'", *graph_name);
    auto* inst = resolveGraph(*graph_name);

    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = co_await inst->async_meta->createLabel(*name, defs);
    auto resp = std::make_unique<thrift::LabelInfo>();

    if (label_id == INVALID_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    co_await inst->async_data->createLabel(label_id);

    resp->id() = label_id;
    resp->name() = *name;
    for (auto& d : defs) {
        thrift::PropertyDefThrift pd;
        pd.name() = d.name;
        pd.type() = static_cast<thrift::PropertyType>(static_cast<int>(d.type));
        pd.is_required() = d.required;
        resp->properties()->push_back(std::move(pd));
    }

    spdlog::info("[handler] createLabel '{}' done on graph '{}', id={}, {} properties, took={}ms", *name, *graph_name,
                 label_id, defs.size(), nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::LabelInfo>>>
EuGraphHandler::co_listLabels(std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    spdlog::info("[handler] listLabels start, graph='{}'", *graph_name);
    auto* inst = resolveGraph(*graph_name);

    auto labels = co_await inst->async_meta->listLabels();
    auto resp = std::make_unique<std::vector<thrift::LabelInfo>>();

    for (const auto& l : labels) {
        if (l.name == kAnonLabelName)
            continue; // internal label, not user-visible
        thrift::LabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        for (const auto& pd : l.properties) {
            thrift::PropertyDefThrift p;
            p.name() = pd.name;
            p.type() = fromPropertyType(pd.type);
            p.is_required() = pd.required;
            info.properties()->push_back(std::move(p));
        }
        resp->push_back(std::move(info));
    }

    spdlog::info("[handler] listLabels done, took={}ms", nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<thrift::EdgeLabelInfo>>
EuGraphHandler::co_createEdgeLabel(std::unique_ptr<std::string> name,
                                   std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties,
                                   std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    spdlog::info("[handler] createEdgeLabel start, graph='{}'", *graph_name);
    auto* inst = resolveGraph(*graph_name);

    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = co_await inst->async_meta->createEdgeLabel(*name, defs);
    auto resp = std::make_unique<thrift::EdgeLabelInfo>();

    if (label_id == INVALID_EDGE_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    co_await inst->async_data->createEdgeLabel(label_id);

    resp->id() = label_id;
    resp->name() = *name;
    resp->directed() = true;

    spdlog::info("[handler] createEdgeLabel '{}' done on graph '{}', id={}, took={}ms", *name, *graph_name, label_id,
                 nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>>
EuGraphHandler::co_listEdgeLabels(std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    spdlog::info("[handler] listEdgeLabels start, graph='{}'", *graph_name);
    auto* inst = resolveGraph(*graph_name);

    auto labels = co_await inst->async_meta->listEdgeLabels();
    auto resp = std::make_unique<std::vector<thrift::EdgeLabelInfo>>();

    for (const auto& l : labels) {
        thrift::EdgeLabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        info.directed() = l.directed;
        for (const auto& pd : l.properties) {
            thrift::PropertyDefThrift p;
            p.name() = pd.name;
            p.type() = fromPropertyType(pd.type);
            p.is_required() = pd.required;
            info.properties()->push_back(std::move(p));
        }
        resp->push_back(std::move(info));
    }

    spdlog::info("[handler] listEdgeLabels done, took={}ms", nowMs() - t0);
    co_return resp;
}

// ==================== DML: Cypher (streaming) ====================

folly::coro::Task<apache::thrift::ResponseAndServerStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>>
EuGraphHandler::co_executeCypher(std::unique_ptr<std::string> query, std::unique_ptr<std::string> graph_name,
                                 std::unique_ptr<std::map<std::string, std::string>> parameters) {
    auto t0 = nowMs();
    spdlog::info("[handler] executeCypher start, graph='{}', query='{}'", *graph_name, *query);
    auto* inst = resolveGraph(*graph_name);

    // Parse parameter values from Cypher literal strings to runtime Values
    std::unordered_map<std::string, Value> params;
    if (parameters && !parameters->empty()) {
        static thread_local cypher::CypherQueryParser parser;
        for (const auto& [name, literal_str] : *parameters) {
            auto result = parser.parse("RETURN " + literal_str);
            if (std::holds_alternative<cypher::Statement>(result)) {
                auto& stmt = std::get<cypher::Statement>(result);
                auto* rq = std::get_if<std::unique_ptr<cypher::RegularQuery>>(&stmt);
                if (rq && *rq && !(*rq)->first.clauses.empty()) {
                    auto* ret = std::get_if<std::unique_ptr<cypher::ReturnClause>>(&(*rq)->first.clauses[0]);
                    if (ret && *ret && !(*ret)->items.empty()) {
                        const auto& expr = (*ret)->items[0].expr;
                        auto* lit = std::get_if<std::unique_ptr<cypher::Literal>>(&expr);
                        if (lit && *lit) {
                            std::visit(
                                [&params, &name](const auto& v) {
                                    using V = std::decay_t<decltype(v)>;
                                    if constexpr (!std::is_same_v<V, cypher::NullValue>) {
                                        params[name] = Value(v);
                                    }
                                },
                                (*lit)->value);
                        }
                    }
                }
            }
        }
    }

    auto ctx = co_await inst->executor->prepareStream(*query, params);

    if (!ctx->error.empty()) {
        spdlog::info("[handler] executeCypher error: {}, took={}ms", ctx->error, nowMs() - t0);
        throw std::runtime_error(ctx->error);
    }

    auto labels = co_await inst->async_meta->listLabels();
    auto edge_labels = co_await inst->async_meta->listEdgeLabels();
    std::unordered_map<LabelId, LabelDef> label_defs;
    for (const auto& l : labels)
        label_defs[l.id] = l;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    for (const auto& el : edge_labels)
        edge_label_defs[el.id] = el;

    thrift::QueryStreamMeta meta;
    meta.columns() = std::move(ctx->columns);

    auto gen = makeStreamGenerator(std::move(ctx), std::move(label_defs), std::move(edge_label_defs), *this, t0);

    co_return apache::thrift::ResponseAndServerStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>{std::move(meta),
                                                                                                       std::move(gen)};
}

// ==================== Batch Import ====================

PropertyValue EuGraphHandler::thriftToPropertyValue(const thrift::PropertyValueThrift& v) {
    switch (v.getType()) {
    case thrift::PropertyValueThrift::Type::bool_val:
        return v.get_bool_val();
    case thrift::PropertyValueThrift::Type::int_val:
        return v.get_int_val();
    case thrift::PropertyValueThrift::Type::double_val:
        return v.get_double_val();
    case thrift::PropertyValueThrift::Type::string_val:
        return std::string(v.get_string_val());
    case thrift::PropertyValueThrift::Type::int_array: {
        const auto& arr = v.get_int_array();
        return std::vector<int64_t>(arr.begin(), arr.end());
    }
    case thrift::PropertyValueThrift::Type::double_array: {
        const auto& arr = v.get_double_array();
        return std::vector<double>(arr.begin(), arr.end());
    }
    case thrift::PropertyValueThrift::Type::string_array: {
        const auto& arr = v.get_string_array();
        return std::vector<std::string>(arr.begin(), arr.end());
    }
    case thrift::PropertyValueThrift::Type::datetime_val: {
        const auto& dt = v.get_datetime_val();
        DateTimeValue tv;
        switch (*dt.kind()) {
        case thrift::DateTimeKind::DATE:
            tv.kind = DateTimeKind::DATE;
            break;
        case thrift::DateTimeKind::LOCAL_DATETIME:
            tv.kind = DateTimeKind::LOCAL_DATETIME;
            break;
        case thrift::DateTimeKind::DATETIME_WITH_TZ:
            tv.kind = DateTimeKind::DATETIME;
            break;
        }
        tv.year = *dt.year();
        tv.month = *dt.month();
        tv.day = *dt.day();
        tv.hour = *dt.hour();
        tv.minute = *dt.minute();
        tv.second = *dt.second();
        tv.nanos = *dt.nanos();
        tv.tz_offset_sec = *dt.tz_offset_min() * 60;
        tv.tz_name = *dt.tz_name();
        return tv;
    }
    case thrift::PropertyValueThrift::Type::time_val: {
        const auto& t = v.get_time_val();
        TimeValue tv;
        switch (*t.kind()) {
        case thrift::TimeKind::LOCAL_TIME:
            tv.kind = TimeKind::LOCAL_TIME;
            break;
        case thrift::TimeKind::TIME_WITH_TZ:
            tv.kind = TimeKind::TIME;
            break;
        }
        tv.hour = *t.hour();
        tv.minute = *t.minute();
        tv.second = *t.second();
        tv.nanos = *t.nanos();
        tv.tz_offset_sec = *t.tz_offset_min() * 60;
        tv.tz_name = *t.tz_name();
        return tv;
    }
    case thrift::PropertyValueThrift::Type::duration_val: {
        const auto& d = v.get_duration_val();
        DurationValue dv;
        dv.months = *d.months();
        dv.days = *d.days();
        dv.seconds = *d.seconds();
        dv.nanos = *d.nanos();
        return dv;
    }
    case thrift::PropertyValueThrift::Type::datetime_array: {
        const auto& arr = v.get_datetime_array();
        std::vector<DateTimeValue> result;
        result.reserve(arr.size());
        for (const auto& dt : arr) {
            DateTimeValue tv;
            switch (*dt.kind()) {
            case thrift::DateTimeKind::DATE:
                tv.kind = DateTimeKind::DATE;
                break;
            case thrift::DateTimeKind::LOCAL_DATETIME:
                tv.kind = DateTimeKind::LOCAL_DATETIME;
                break;
            case thrift::DateTimeKind::DATETIME_WITH_TZ:
                tv.kind = DateTimeKind::DATETIME;
                break;
            }
            tv.year = *dt.year();
            tv.month = *dt.month();
            tv.day = *dt.day();
            tv.hour = *dt.hour();
            tv.minute = *dt.minute();
            tv.second = *dt.second();
            tv.nanos = *dt.nanos();
            tv.tz_offset_sec = *dt.tz_offset_min() * 60;
            tv.tz_name = *dt.tz_name();
            result.push_back(std::move(tv));
        }
        return result;
    }
    case thrift::PropertyValueThrift::Type::time_array: {
        const auto& arr = v.get_time_array();
        std::vector<TimeValue> result;
        result.reserve(arr.size());
        for (const auto& t : arr) {
            TimeValue tv;
            switch (*t.kind()) {
            case thrift::TimeKind::LOCAL_TIME:
                tv.kind = TimeKind::LOCAL_TIME;
                break;
            case thrift::TimeKind::TIME_WITH_TZ:
                tv.kind = TimeKind::TIME;
                break;
            }
            tv.hour = *t.hour();
            tv.minute = *t.minute();
            tv.second = *t.second();
            tv.nanos = *t.nanos();
            tv.tz_offset_sec = *t.tz_offset_min() * 60;
            tv.tz_name = *t.tz_name();
            result.push_back(std::move(tv));
        }
        return result;
    }
    case thrift::PropertyValueThrift::Type::duration_array: {
        const auto& arr = v.get_duration_array();
        std::vector<DurationValue> result;
        result.reserve(arr.size());
        for (const auto& d : arr) {
            DurationValue dv;
            dv.months = *d.months();
            dv.days = *d.days();
            dv.seconds = *d.seconds();
            dv.nanos = *d.nanos();
            result.push_back(std::move(dv));
        }
        return result;
    }
    default:
        return std::monostate{};
    }
}

folly::coro::Task<std::unique_ptr<thrift::BatchInsertVerticesResult>>
EuGraphHandler::co_batchInsertVertices(std::unique_ptr<std::string> label_name,
                                       std::unique_ptr<std::vector<thrift::VertexRecord>> records,
                                       std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    auto count = records->size();
    spdlog::info("[handler] batchInsertVertices start, graph='{}', label='{}', count={}", *graph_name, *label_name,
                 count);
    auto* inst = resolveGraph(*graph_name);

    auto label_id_opt = co_await inst->async_meta->getLabelId(*label_name);
    if (!label_id_opt.has_value()) {
        throw std::runtime_error("Label not found: " + *label_name);
    }
    LabelId label_id = *label_id_opt;

    VertexId start_vid = co_await inst->async_meta->nextVertexIdRange(count);

    std::vector<IAsyncGraphDataStore::BatchVertexEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto& rec = (*records)[i];
        IAsyncGraphDataStore::BatchVertexEntry entry;
        entry.vid = start_vid + i;
        for (const auto& pv : *rec.properties()) {
            entry.props.push_back(thriftToPropertyValue(pv));
        }
        entries.push_back(std::move(entry));
    }

    co_await inst->async_data->batchInsertVertices(label_id, std::move(entries));

    auto resp = std::make_unique<thrift::BatchInsertVerticesResult>();
    resp->vertex_ids()->reserve(count);
    for (size_t i = 0; i < count; i++) {
        resp->vertex_ids()->push_back(static_cast<int64_t>(start_vid + i));
    }
    resp->count() = static_cast<int32_t>(count);
    spdlog::info("[handler] batchInsertVertices done, count={}, took={}ms", count, nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::int32_t>
EuGraphHandler::co_batchInsertEdges(std::unique_ptr<std::string> edge_label_name,
                                    std::unique_ptr<std::vector<thrift::EdgeRecord>> records,
                                    std::unique_ptr<std::string> graph_name) {
    auto t0 = nowMs();
    auto count = records->size();
    spdlog::info("[handler] batchInsertEdges start, graph='{}', edgeLabel='{}', count={}", *graph_name,
                 *edge_label_name, count);
    auto* inst = resolveGraph(*graph_name);

    auto elabel_id_opt = co_await inst->async_meta->getEdgeLabelId(*edge_label_name);
    if (!elabel_id_opt.has_value()) {
        throw std::runtime_error("EdgeLabel not found: " + *edge_label_name);
    }
    EdgeLabelId elabel_id = *elabel_id_opt;

    EdgeId start_eid = co_await inst->async_meta->nextEdgeIdRange(count);

    std::vector<IAsyncGraphDataStore::BatchEdgeEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto& rec = (*records)[i];
        IAsyncGraphDataStore::BatchEdgeEntry entry;
        entry.eid = start_eid + i;
        entry.src_id = static_cast<VertexId>(rec.src_vertex_id().value());
        entry.dst_id = static_cast<VertexId>(rec.dst_vertex_id().value());
        entry.seq = i;
        for (const auto& pv : *rec.properties()) {
            entry.props.push_back(thriftToPropertyValue(pv));
        }
        entries.push_back(std::move(entry));
    }

    co_await inst->async_data->batchInsertEdges(elabel_id, std::move(entries));

    spdlog::info("[handler] batchInsertEdges done, count={}, took={}ms", count, nowMs() - t0);
    co_return static_cast<std::int32_t>(count);
}

} // namespace server
} // namespace eugraph
