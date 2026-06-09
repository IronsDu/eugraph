#include "thrift_fmt/result_format.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eugraph::thrift_fmt {
namespace {

std::string convertVertexJson(const std::string& json) {
    if (json.empty())
        return "()";
    try {
        auto j = nlohmann::json::parse(json);
        std::ostringstream oss;
        oss << "(";
        if (j.contains("labels") && j["labels"].is_array()) {
            for (const auto& l : j["labels"])
                oss << ":" << l.get<std::string>();
        } else if (j.contains("label") && j["label"].is_string()) {
            oss << ":" << j["label"].get<std::string>();
        }
        std::vector<std::pair<std::string, std::string>> props;
        for (auto& [key, value] : j.items()) {
            if (key == "id" || key == "label" || key == "labels")
                continue;
            std::string formattedVal;
            if (value.is_string()) {
                formattedVal = "'" + value.get<std::string>() + "'";
            } else if (value.is_number_integer()) {
                formattedVal = std::to_string(value.get<int64_t>());
            } else if (value.is_number_float()) {
                double dv = value.get<double>();
                if (std::isnan(dv))
                    formattedVal = "NaN";
                else if (std::isinf(dv))
                    formattedVal = dv > 0 ? "Infinity" : "-Infinity";
                else {
                    std::ostringstream dss;
                    dss << std::setprecision(std::numeric_limits<double>::digits10) << dv;
                    formattedVal = dss.str();
                    if (formattedVal.find('.') == std::string::npos && formattedVal.find('e') == std::string::npos &&
                        formattedVal.find('E') == std::string::npos)
                        formattedVal += ".0";
                }
            } else if (value.is_boolean()) {
                formattedVal = value.get<bool>() ? "true" : "false";
            } else if (value.is_null()) {
                formattedVal = "null";
            } else if (value.is_array()) {
                formattedVal = value.dump();
            } else {
                formattedVal = value.dump();
            }
            props.emplace_back(key, formattedVal);
        }
        if (!props.empty()) {
            // Space before properties only when there is preceding content (labels)
            if (oss.tellp() > 1)
                oss << ' ';
            oss << "{";
            for (size_t i = 0; i < props.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << props[i].first << ": " << props[i].second;
            }
            oss << "}";
        }
        oss << ")";
        return oss.str();
    } catch (...) {
        return "()";
    }
}

std::string convertEdgeJson(const std::string& json) {
    if (json.empty())
        return "[]";
    try {
        auto j = nlohmann::json::parse(json);
        std::ostringstream oss;
        oss << "[";
        if (j.contains("labels") && j["labels"].is_array()) {
            for (const auto& l : j["labels"])
                oss << ":" << l.get<std::string>();
        } else if (j.contains("label") && j["label"].is_string()) {
            oss << ":" << j["label"].get<std::string>();
        }
        std::vector<std::pair<std::string, std::string>> props;
        for (auto& [key, value] : j.items()) {
            if (key == "id" || key == "src" || key == "dst" || key == "label")
                continue;
            std::string formattedVal;
            if (value.is_string()) {
                formattedVal = "'" + value.get<std::string>() + "'";
            } else if (value.is_number_integer()) {
                formattedVal = std::to_string(value.get<int64_t>());
            } else if (value.is_number_float()) {
                double dv = value.get<double>();
                if (std::isnan(dv))
                    formattedVal = "NaN";
                else if (std::isinf(dv))
                    formattedVal = dv > 0 ? "Infinity" : "-Infinity";
                else {
                    std::ostringstream dss;
                    dss << std::setprecision(std::numeric_limits<double>::digits10) << dv;
                    formattedVal = dss.str();
                    if (formattedVal.find('.') == std::string::npos && formattedVal.find('e') == std::string::npos &&
                        formattedVal.find('E') == std::string::npos)
                        formattedVal += ".0";
                }
            } else if (value.is_boolean()) {
                formattedVal = value.get<bool>() ? "true" : "false";
            } else if (value.is_null()) {
                formattedVal = "null";
            } else {
                formattedVal = value.dump();
            }
            props.emplace_back(key, formattedVal);
        }
        if (!props.empty()) {
            // Space before properties only when there is preceding content (label)
            if (oss.tellp() > 1)
                oss << ' ';
            oss << "{";
            for (size_t i = 0; i < props.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << props[i].first << ": " << props[i].second;
            }
            oss << "}";
        }
        oss << "]";
        return oss.str();
    } catch (...) {
        return "[]";
    }
}

} // namespace

std::string formatResultValue(const thrift::ResultValue& val) {
    switch (val.getType()) {
    case thrift::ResultValue::Type::bool_val:
        return val.get_bool_val() ? "true" : "false";

    case thrift::ResultValue::Type::int_val:
        return std::to_string(val.get_int_val());

    case thrift::ResultValue::Type::double_val:
        return formatDouble(val.get_double_val());

    case thrift::ResultValue::Type::string_val:
        return "'" + val.get_string_val() + "'";

    case thrift::ResultValue::Type::vertex_json:
        return convertVertexJson(val.get_vertex_json());

    case thrift::ResultValue::Type::edge_json:
        return convertEdgeJson(val.get_edge_json());

    case thrift::ResultValue::Type::path_json:
        return val.get_path_json();

    case thrift::ResultValue::Type::list_json:
        return val.get_list_json();

    case thrift::ResultValue::Type::map_json:
        return val.get_map_json();

    default:
        return "null";
    }
}

std::string propertyTypeToString(thrift::PropertyType t) {
    switch (t) {
    case thrift::PropertyType::BOOL:
        return "BOOL";
    case thrift::PropertyType::INT64:
        return "INT64";
    case thrift::PropertyType::DOUBLE:
        return "DOUBLE";
    case thrift::PropertyType::STRING:
        return "STRING";
    case thrift::PropertyType::INT64_ARRAY:
        return "INT64_ARRAY";
    case thrift::PropertyType::DOUBLE_ARRAY:
        return "DOUBLE_ARRAY";
    case thrift::PropertyType::STRING_ARRAY:
        return "STRING_ARRAY";
    case thrift::PropertyType::DATETIME:
        return "DATETIME";
    case thrift::PropertyType::TIME:
        return "TIME";
    case thrift::PropertyType::DURATION:
        return "DURATION";
    case thrift::PropertyType::DATETIME_ARRAY:
        return "DATETIME_ARRAY";
    case thrift::PropertyType::TIME_ARRAY:
        return "TIME_ARRAY";
    case thrift::PropertyType::DURATION_ARRAY:
        return "DURATION_ARRAY";
    }
    return "UNKNOWN";
}

thrift::PropertyType parsePropertyType(const std::string& type_str) {
    std::string upper = type_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "BOOL")
        return thrift::PropertyType::BOOL;
    if (upper == "INT64" || upper == "INT" || upper == "INTEGER")
        return thrift::PropertyType::INT64;
    if (upper == "DOUBLE" || upper == "FLOAT")
        return thrift::PropertyType::DOUBLE;
    if (upper == "DATETIME" || upper == "DATE")
        return thrift::PropertyType::DATETIME;
    if (upper == "TIME")
        return thrift::PropertyType::TIME;
    if (upper == "DURATION")
        return thrift::PropertyType::DURATION;
    return thrift::PropertyType::STRING;
}

} // namespace eugraph::thrift_fmt
