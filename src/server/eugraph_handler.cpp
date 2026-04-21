#include "server/eugraph_handler.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>

#include <folly/io/async/EventBaseManager.h>

namespace {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:   oss << c; break;
            }
        }
        oss << '"';
    } else if (std::holds_alternative<std::vector<int64_t>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<int64_t>>(pv)) {
            if (!first) oss << ',';
            oss << x;
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<double>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& x : std::get<std::vector<double>>(pv)) {
            if (!first) oss << ',';
            oss << x;
            first = false;
        }
        oss << ']';
    } else if (std::holds_alternative<std::vector<std::string>>(pv)) {
        oss << '[';
        bool first = true;
        for (auto& s : std::get<std::vector<std::string>>(pv)) {
            if (!first) oss << ',';
            oss << '"' << s << '"';
            first = false;
        }
        oss << ']';
    } else {
        oss << "null";
    }
}

} // anonymous namespace

thrift::ResultValue EuGraphHandler::valueToThrift(
    const Value& val,
    const std::unordered_map<LabelId, LabelDef>& label_defs,
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
                if (it == label_defs.end()) continue;
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

    auto label_id = co_await meta_.createLabel(*name, defs);
    auto resp = std::make_unique<thrift::LabelInfo>();

    if (label_id == INVALID_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    store_.createLabel(label_id);

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
    auto labels = co_await meta_.listLabels();
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

    auto label_id = co_await meta_.createEdgeLabel(*name, defs);
    auto resp = std::make_unique<thrift::EdgeLabelInfo>();

    if (label_id == INVALID_EDGE_LABEL_ID) {
        resp->id() = 0;
        resp->name() = *name;
        co_return resp;
    }

    store_.createEdgeLabel(label_id);

    resp->id() = label_id;
    resp->name() = *name;
    resp->directed() = true;

    spdlog::info("[handler] createEdgeLabel '{}' done, id={}, took={}ms", *name, label_id, nowMs() - t0);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>> EuGraphHandler::co_listEdgeLabels() {
    auto t0 = nowMs();
    spdlog::info("[handler] listEdgeLabels start");
    auto labels = co_await meta_.listEdgeLabels();
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

// ==================== DML: Cypher ====================

folly::coro::Task<std::unique_ptr<thrift::QueryResult>>
EuGraphHandler::co_executeCypher(std::unique_ptr<std::string> query) {
    auto t0 = nowMs();
    spdlog::info("[handler] executeCypher start, query='{}'", *query);

    // Capture this IO thread's EventBase before switching to compute pool.
    // Each IO thread has its own EventBase with its own epoll loop.
    // After compute work, we switch back to THIS specific EventBase to wake
    // it up immediately, rather than submitting to the IO pool and hoping
    // the right thread picks it up.
    auto* ioEvb = folly::EventBaseManager::get()->getEventBase();

    auto result = co_await executor_.executeAsync(*query);

    // Switch back to the specific IO thread that received this request.
    // co_withExecutor(ioEvb, noop) schedules on this EventBase via
    // EventBase::add(), which writes to its eventfd and wakes its epoll_wait.
    co_await folly::coro::co_withExecutor(
        ioEvb,
        folly::coro::co_invoke([]() -> folly::coro::Task<void> { co_return; }));
    auto resp = std::make_unique<thrift::QueryResult>();

    if (!result.error.empty()) {
        resp->error() = std::move(result.error);
        co_return resp;
    }

    // Fetch label definitions for vertex/edge JSON serialization
    auto labels = co_await meta_.listLabels();
    auto edge_labels = co_await meta_.listEdgeLabels();
    std::unordered_map<LabelId, LabelDef> label_defs;
    for (const auto& l : labels)
        label_defs[l.id] = l;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    for (const auto& el : edge_labels)
        edge_label_defs[el.id] = el;

    resp->columns() = std::move(result.columns);

    for (const auto& row : result.rows) {
        thrift::ResultRow row_resp;
        for (const auto& val : row) {
            row_resp.values()->push_back(valueToThrift(val, label_defs, edge_label_defs));
        }
        resp->rows()->push_back(std::move(row_resp));
    }

    resp->rows_affected() = static_cast<int64_t>(result.rows.size());
    spdlog::info("[handler] executeCypher done, {} rows, took={}ms", result.rows.size(), nowMs() - t0);
    co_return resp;
}

} // namespace server
} // namespace eugraph
