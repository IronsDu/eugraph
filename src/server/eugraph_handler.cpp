#include "server/eugraph_handler.hpp"

#include <spdlog/spdlog.h>

#include <sstream>

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

thrift::ResultValue EuGraphHandler::valueToThrift(const Value& val) {
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
        rv.set_vertex_json("Vertex(id=" + std::to_string(v.id) + ")");
    } else if (std::holds_alternative<EdgeValue>(val)) {
        auto& e = std::get<EdgeValue>(val);
        std::ostringstream oss;
        oss << "Edge(id=" << e.id << ", " << e.src_id << "->" << e.dst_id << ")";
        rv.set_edge_json(oss.str());
    }

    return rv;
}

// ==================== DDL: Label ====================

folly::coro::Task<std::unique_ptr<thrift::LabelInfo>>
EuGraphHandler::co_createLabel(std::unique_ptr<std::string> name,
                               std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
    spdlog::info("start execute create label");
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

    spdlog::info("Created label '{}' with id={}, {} properties", *name, label_id, defs.size());
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::LabelInfo>>> EuGraphHandler::co_listLabels() {
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

    co_return resp;
}

// ==================== DDL: EdgeLabel ====================

folly::coro::Task<std::unique_ptr<thrift::EdgeLabelInfo>>
EuGraphHandler::co_createEdgeLabel(std::unique_ptr<std::string> name,
                                   std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
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

    spdlog::info("Created edge label '{}' with id={}", *name, label_id);
    co_return resp;
}

folly::coro::Task<std::unique_ptr<std::vector<thrift::EdgeLabelInfo>>> EuGraphHandler::co_listEdgeLabels() {
    auto labels = co_await meta_.listEdgeLabels();
    auto resp = std::make_unique<std::vector<thrift::EdgeLabelInfo>>();

    for (const auto& l : labels) {
        thrift::EdgeLabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        info.directed() = l.directed;
        resp->push_back(std::move(info));
    }

    co_return resp;
}

// ==================== DML: Cypher ====================

folly::coro::Task<std::unique_ptr<thrift::QueryResult>>
EuGraphHandler::co_executeCypher(std::unique_ptr<std::string> query) {
    auto result = executor_.executeSync(*query);
    auto resp = std::make_unique<thrift::QueryResult>();

    if (!result.error.empty()) {
        resp->error() = std::move(result.error);
        co_return resp;
    }

    resp->columns() = std::move(result.columns);

    for (const auto& row : result.rows) {
        thrift::ResultRow row_resp;
        for (const auto& val : row) {
            row_resp.values()->push_back(valueToThrift(val));
        }
        resp->rows()->push_back(std::move(row_resp));
    }

    resp->rows_affected() = static_cast<int64_t>(result.rows.size());
    co_return resp;
}

} // namespace server
} // namespace eugraph
