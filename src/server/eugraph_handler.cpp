#include "server/eugraph_handler.hpp"

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

folly::coro::AsyncGenerator<eugraph::thrift::ResultRowBatch&&> makeStreamGenerator(
    std::shared_ptr<eugraph::compute::StreamContext> ctx,
    std::unordered_map<eugraph::LabelId, eugraph::LabelDef> label_defs,
    std::unordered_map<eugraph::EdgeLabelId, eugraph::EdgeLabelDef> edge_label_defs,
    eugraph::server::EuGraphHandler* handler,
    int64_t t0) {
    size_t total_rows = 0;
    while (auto batch = co_await ctx->gen.next()) {
        eugraph::thrift::ResultRowBatch thrift_batch;
        for (auto& row : batch->rows) {
            eugraph::thrift::ResultRow row_resp;
            for (auto& val : row) {
                row_resp.values()->push_back(handler->valueToThrift(val, label_defs, edge_label_defs));
            }
            thrift_batch.rows()->push_back(std::move(row_resp));
        }
        total_rows += batch->rows.size();
        co_yield std::move(thrift_batch);
    }
    co_await ctx->store->commitTran(ctx->txn);
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
    }
    return ::eugraph::PropertyType::STRING;
}

PropertyDef EuGraphHandler::toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id) {
    PropertyDef def;
    def.id = id;
    def.name = req.name().value();
    def.type = toPropertyType(req.type().value());
    def.required = req.is_required().value();
    return def;
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

        // Collect property name→value pairs from all labels of this vertex
        if (v.properties.has_value() && v.labels.has_value()) {
            for (LabelId lid : *v.labels) {
                auto it = label_defs.find(lid);
                if (it == label_defs.end())
                    continue;
                for (const auto& pd : it->second.properties) {
                    if (pd.id < v.properties->size()) {
                        const auto& pv = (*v.properties)[pd.id];
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

        // Resolve edge label name
        auto elit = edge_label_defs.find(e.label_id);
        if (elit != edge_label_defs.end()) {
            oss << ",\"label\":\"" << elit->second.name << '"';
        }

        // Collect properties
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
    }

    return rv;
}

// ==================== DDL: Label ====================

folly::coro::Task<std::unique_ptr<thrift::LabelInfo>>
EuGraphHandler::co_createLabel(std::unique_ptr<std::string> name,
                               std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
    auto t0 = nowMs();
    spdlog::info("[handler] createLabel start");
    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = co_await async_meta_.createLabel(*name, defs);
    auto resp = std::make_unique<thrift::LabelInfo>();

    if (label_id == INVALID_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    co_await async_data_.createLabel(label_id);

    resp->id() = label_id;
    resp->name() = *name;
    for (auto& d : defs) {
        thrift::PropertyDefThrift pd;
        pd.name() = d.name;
        pd.type() = static_cast<thrift::PropertyType>(static_cast<int>(d.type));
        pd.is_required() = d.required;
        resp->properties()->push_back(std::move(pd));
    }

    spdlog::info("[handler] createLabel '{}' done, id={}, {} properties, took={}ms", *name, label_id, defs.size(),
                 nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::LabelInfo>>> EuGraphHandler::co_listLabels() {
    auto t0 = nowMs();
    spdlog::info("[handler] listLabels start");
    auto labels = co_await async_meta_.listLabels();
    auto resp = std::make_unique<std::vector<thrift::LabelInfo>>();

    for (const auto& l : labels) {
        thrift::LabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        for (const auto& pd : l.properties) {
            thrift::PropertyDefThrift p;
            p.name() = pd.name;
            p.type() = static_cast<thrift::PropertyType>(static_cast<int>(pd.type));
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
                                   std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
    auto t0 = nowMs();
    spdlog::info("[handler] createEdgeLabel start");
    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = co_await async_meta_.createEdgeLabel(*name, defs);
    auto resp = std::make_unique<thrift::EdgeLabelInfo>();

    if (label_id == INVALID_EDGE_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    spdlog::info("{} 111111111", __FUNCTION__);
    co_await async_data_.createEdgeLabel(label_id);

    spdlog::info("{} 222222222222", __FUNCTION__);
    resp->id() = label_id;
    resp->name() = *name;
    resp->directed() = true;

    spdlog::info("[handler] createEdgeLabel '{}' done, id={}, took={}ms", *name, label_id, nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>> EuGraphHandler::co_listEdgeLabels() {
    auto t0 = nowMs();
    spdlog::info("[handler] listEdgeLabels start");
    auto labels = co_await async_meta_.listEdgeLabels();
    auto resp = std::make_unique<std::vector<thrift::EdgeLabelInfo>>();

    for (const auto& l : labels) {
        thrift::EdgeLabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        info.directed() = l.directed;
        resp->push_back(std::move(info));
    }

    spdlog::info("[handler] listEdgeLabels done, took={}ms", nowMs() - t0);
    co_return resp;
}

// ==================== DML: Cypher (streaming) ====================

folly::coro::Task<apache::thrift::ResponseAndServerStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>>
EuGraphHandler::co_executeCypher(std::unique_ptr<std::string> query) {
    auto t0 = nowMs();
    spdlog::info("[handler] executeCypher start, query='{}'", *query);

    auto ctx = co_await executor_.prepareStream(*query);

    if (!ctx->error.empty()) {
        spdlog::info("[handler] executeCypher error: {}, took={}ms", ctx->error, nowMs() - t0);
        throw std::runtime_error(ctx->error);
    }

    // Fetch label definitions for vertex/edge JSON serialization
    auto labels = co_await async_meta_.listLabels();
    auto edge_labels = co_await async_meta_.listEdgeLabels();
    std::unordered_map<LabelId, LabelDef> label_defs;
    for (const auto& l : labels)
        label_defs[l.id] = l;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    for (const auto& el : edge_labels)
        edge_label_defs[el.id] = el;

    thrift::QueryStreamMeta meta;
    meta.columns() = std::move(ctx->columns);

    auto gen = makeStreamGenerator(std::move(ctx), std::move(label_defs), std::move(edge_label_defs), this, t0);

    co_return apache::thrift::ResponseAndServerStream<thrift::QueryStreamMeta, thrift::ResultRowBatch>{
        std::move(meta), std::move(gen)};
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
    default:
        return std::monostate{};
    }
}

folly::coro::Task<std::unique_ptr<thrift::BatchInsertVerticesResult>>
EuGraphHandler::co_batchInsertVertices(std::unique_ptr<std::string> label_name,
                                       std::unique_ptr<std::vector<thrift::VertexRecord>> records) {
    auto t0 = nowMs();
    auto count = records->size();
    spdlog::info("[handler] batchInsertVertices start, label='{}', count={}", *label_name, count);

    auto label_id_opt = co_await async_meta_.getLabelId(*label_name);
    if (!label_id_opt.has_value()) {
        throw std::runtime_error("Label not found: " + *label_name);
    }
    LabelId label_id = *label_id_opt;

    VertexId start_vid = co_await async_meta_.nextVertexIdRange(count);

    std::vector<IAsyncGraphDataStore::BatchVertexEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto& rec = (*records)[i];
        IAsyncGraphDataStore::BatchVertexEntry entry;
        entry.vid = start_vid + i;
        entry.pk_value = thriftToPropertyValue(rec.pk_value().value());
        for (const auto& pv : *rec.properties()) {
            entry.props.push_back(thriftToPropertyValue(pv));
        }
        entries.push_back(std::move(entry));
    }

    co_await async_data_.batchInsertVertices(label_id, std::move(entries));

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
                                    std::unique_ptr<std::vector<thrift::EdgeRecord>> records) {
    auto t0 = nowMs();
    auto count = records->size();
    spdlog::info("[handler] batchInsertEdges start, edgeLabel='{}', count={}", *edge_label_name, count);

    auto elabel_id_opt = co_await async_meta_.getEdgeLabelId(*edge_label_name);
    if (!elabel_id_opt.has_value()) {
        throw std::runtime_error("EdgeLabel not found: " + *edge_label_name);
    }
    EdgeLabelId elabel_id = *elabel_id_opt;

    EdgeId start_eid = co_await async_meta_.nextEdgeIdRange(count);

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

    co_await async_data_.batchInsertEdges(elabel_id, std::move(entries));

    spdlog::info("[handler] batchInsertEdges done, count={}, took={}ms", count, nowMs() - t0);
    co_return static_cast<std::int32_t>(count);
}

} // namespace server
} // namespace eugraph
