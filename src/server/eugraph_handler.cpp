#include "server/eugraph_handler.hpp"

#include <folly/coro/BlockingWait.h>
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

std::string propertyTypeToName(::eugraph::PropertyType t) {
    switch (t) {
    case ::eugraph::PropertyType::BOOL: return "BOOL";
    case ::eugraph::PropertyType::INT64: return "INT64";
    case ::eugraph::PropertyType::DOUBLE: return "DOUBLE";
    case ::eugraph::PropertyType::STRING: return "STRING";
    case ::eugraph::PropertyType::INT64_ARRAY: return "INT64_ARRAY";
    case ::eugraph::PropertyType::DOUBLE_ARRAY: return "DOUBLE_ARRAY";
    case ::eugraph::PropertyType::STRING_ARRAY: return "STRING_ARRAY";
    }
    return "UNKNOWN";
}

PropertyDef EuGraphHandler::toPropertyDef(const thrift::PropertyDefThrift& req, uint16_t id) {
    PropertyDef def;
    def.id = id;
    def.name = req.name().value();
    def.type = toPropertyType(req.type().value());
    def.required = req.is_required().value();
    return def;
}

std::string EuGraphHandler::formatProperties(const std::vector<PropertyDef>& props) {
    if (props.empty()) return "(none)";
    std::string result;
    for (size_t i = 0; i < props.size(); i++) {
        if (i > 0) result += ", ";
        result += props[i].name + ":" + propertyTypeToName(props[i].type);
    }
    return result;
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

void EuGraphHandler::sync_createLabel(thrift::LabelInfo& _return,
                                       std::unique_ptr<std::string> name,
                                       std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = folly::coro::blockingWait(meta_.createLabel(*name, defs));
    if (label_id == INVALID_LABEL_ID) {
        _return.id() = 0;
        _return.name() = *name;
        return;
    }

    store_.createLabel(label_id);

    _return.id() = label_id;
    _return.name() = *name;
    for (auto& d : defs) {
        thrift::PropertyDefThrift pd;
        pd.name() = d.name;
        pd.type() = thrift::PropertyType::STRING; // simplified
        for (int i = 1; i <= 7; i++) {
            if (static_cast<int>(d.type) == i) {
                pd.type() = static_cast<thrift::PropertyType>(i);
                break;
            }
        }
        pd.is_required() = d.required;
        _return.properties()->push_back(std::move(pd));
    }
}

void EuGraphHandler::sync_listLabels(std::vector<thrift::LabelInfo>& _return) {
    auto labels = folly::coro::blockingWait(meta_.listLabels());
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
        _return.push_back(std::move(info));
    }
}

// ==================== DDL: EdgeLabel ====================

void EuGraphHandler::sync_createEdgeLabel(thrift::EdgeLabelInfo& _return,
                                            std::unique_ptr<std::string> name,
                                            std::unique_ptr<std::vector<thrift::PropertyDefThrift>> properties) {
    std::vector<PropertyDef> defs;
    for (size_t i = 0; i < properties->size(); i++) {
        defs.push_back(toPropertyDef((*properties)[i], static_cast<uint16_t>(i + 1)));
    }

    auto label_id = folly::coro::blockingWait(meta_.createEdgeLabel(*name, defs));
    if (label_id == INVALID_EDGE_LABEL_ID) {
        _return.id() = 0;
        _return.name() = *name;
        return;
    }

    store_.createEdgeLabel(label_id);

    _return.id() = label_id;
    _return.name() = *name;
    _return.directed() = true;
}

void EuGraphHandler::sync_listEdgeLabels(std::vector<thrift::EdgeLabelInfo>& _return) {
    auto labels = folly::coro::blockingWait(meta_.listEdgeLabels());
    for (const auto& l : labels) {
        thrift::EdgeLabelInfo info;
        info.id() = l.id;
        info.name() = l.name;
        info.directed() = l.directed;
        _return.push_back(std::move(info));
    }
}

// ==================== DML: Cypher ====================

void EuGraphHandler::sync_executeCypher(thrift::QueryResult& _return,
                                          std::unique_ptr<std::string> query) {
    auto result = executor_.executeSync(*query);

    if (!result.error.empty()) {
        _return.error() = std::move(result.error);
        return;
    }

    _return.columns() = std::move(result.columns);

    for (const auto& row : result.rows) {
        thrift::ResultRow row_resp;
        for (const auto& val : row) {
            row_resp.values()->push_back(valueToThrift(val));
        }
        _return.rows()->push_back(std::move(row_resp));
    }

    _return.rows_affected() = static_cast<int64_t>(result.rows.size());
}

} // namespace server
} // namespace eugraph
