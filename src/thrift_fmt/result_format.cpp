#include "thrift_fmt/result_format.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eugraph::thrift_fmt {

// Forward declaration for use by anonymous-namespace helpers.
std::string formatJsonArray(const nlohmann::json& arr);

namespace {

static std::string formatPropValue(const nlohmann::json& value) {
    if (value.is_string())
        return "'" + value.get<std::string>() + "'";
    if (value.is_number_integer())
        return std::to_string(value.get<int64_t>());
    if (value.is_number_float())
        return formatDouble(value.get<double>());
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (value.is_null())
        return "null";
    if (value.is_array())
        return formatJsonArray(value);
    return value.dump();
}

static void collectProps(const nlohmann::json& src, std::vector<std::pair<std::string, std::string>>& props) {
    // Support both ordered array-of-pairs format (new) and legacy object format.
    if (src.contains("properties") && src["properties"].is_array()) {
        for (const auto& pair : src["properties"]) {
            if (pair.is_array() && pair.size() >= 1)
                props.emplace_back(pair[0].get<std::string>(), pair.size() >= 2 ? formatPropValue(pair[1]) : "null");
        }
    } else if (src.contains("properties") && src["properties"].is_object()) {
        for (auto& [key, value] : src["properties"].items())
            props.emplace_back(key, formatPropValue(value));
    }
}

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
        }
        std::vector<std::pair<std::string, std::string>> props;
        collectProps(j, props);
        if (!props.empty()) {
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
        if (j.contains("type") && j["type"].is_string())
            oss << ":" << j["type"].get<std::string>();
        std::vector<std::pair<std::string, std::string>> props;
        collectProps(j, props);
        if (!props.empty()) {
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

std::string convertMapJson(const std::string& json);

// Extract a balanced value from a Cypher-formatted map string starting at pos.
// Values can be: Cypher strings '...', JSON objects {...}, JSON arrays [...],
// and unquoted scalars (numbers, bools, null).
// Advances pos past the extracted value.
std::string extractBalancedValue(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(s[pos]))
        pos++;
    if (pos >= s.size())
        return "";

    char open = s[pos];
    if (open == '\'') {
        // Cypher single-quoted string
        size_t val_start = pos + 1;
        pos++;
        while (pos < s.size() && s[pos] != '\'')
            pos++;
        std::string val = s.substr(val_start, pos - val_start);
        if (pos < s.size())
            pos++; // skip closing quote
        return "'" + val + "'";
    }

    if (open != '{' && open != '[' && open != '"') {
        size_t start = pos;
        while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']')
            pos++;
        std::string val = s.substr(start, pos - start);
        while (!val.empty() && std::isspace(val.back()))
            val.pop_back();
        return val;
    }

    char close = (open == '{') ? '}' : (open == '[') ? ']' : '"';
    if (open == '"') {
        // JSON double-quoted string
        size_t val_start = pos + 1;
        pos++;
        while (pos < s.size() && !(s[pos] == '"' && s[pos - 1] != '\\'))
            pos++;
        std::string val = s.substr(val_start, pos - val_start);
        if (pos < s.size())
            pos++;
        return "'" + val + "'";
    }

    int depth = 1;
    size_t start = pos;
    pos++;
    bool in_string = false;

    while (pos < s.size() && depth > 0) {
        char c = s[pos];
        if (in_string) {
            if (c == '"' && s[pos - 1] != '\\')
                in_string = false;
        } else {
            if (c == '"')
                in_string = true;
            else if (c == open)
                depth++;
            else if (c == close)
                depth--;
        }
        pos++;
    }
    return s.substr(start, pos - start);
}

// Format a single value extracted from a map. Returns formatted representation.
std::string formatMapValue(const std::string& val) {
    if (val.empty())
        return val;
    char first = val[0];

    if (first == '{') {
        try {
            auto j = nlohmann::json::parse(val);
            if (j.is_object()) {
                if (j.contains("start") || j.contains("end"))
                    return convertEdgeJson(val);
                if (j.contains("labels") || j.contains("id"))
                    return convertVertexJson(val);
            }
        } catch (...) {}
        return convertMapJson(val);
    }

    if (first == '[') {
        try {
            auto arr = nlohmann::json::parse(val);
            if (arr.is_array())
                return formatJsonArray(arr);
        } catch (...) {}
        return val;
    }

    return val;
}

std::string convertMapJson(const std::string& json) {
    if (json.empty() || json.size() < 2 || json[0] != '{')
        return json;

    std::ostringstream oss;
    oss << "{";

    size_t pos = 1;
    bool first = true;

    while (pos < json.size()) {
        while (pos < json.size() && std::isspace(json[pos]))
            pos++;
        if (pos >= json.size() || json[pos] == '}')
            break;

        if (!first) {
            if (json[pos] == ',')
                pos++;
            while (pos < json.size() && std::isspace(json[pos]))
                pos++;
            oss << ", ";
        }
        first = false;

        size_t key_start = pos;
        while (pos < json.size() && json[pos] != ':')
            pos++;
        std::string key = json.substr(key_start, pos - key_start);
        while (!key.empty() && std::isspace(key.back()))
            key.pop_back();

        if (pos < json.size())
            pos++;

        std::string value = extractBalancedValue(json, pos);
        oss << key << ": " << formatMapValue(value);
    }

    oss << "}";
    return oss.str();
}

} // namespace

std::string formatJsonArray(const nlohmann::json& arr) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i > 0)
            oss << ", ";
        auto& elem = arr[i];
        if (elem.is_array()) {
            oss << formatJsonArray(elem);
        } else if (elem.is_object() && (elem.contains("start") || elem.contains("end"))) {
            oss << convertEdgeJson(elem.dump());
        } else if (elem.is_object() && (elem.contains("labels") || elem.contains("id"))) {
            oss << convertVertexJson(elem.dump());
        } else if (elem.is_string()) {
            std::string escaped;
            for (char c : elem.get<std::string>()) {
                if (c == '\\')
                    escaped += "\\\\";
                else if (c == '\'')
                    escaped += "\\'";
                else
                    escaped += c;
            }
            oss << "'" << escaped << "'";
        } else if (elem.is_number_integer()) {
            oss << elem.get<int64_t>();
        } else if (elem.is_number_float()) {
            oss << formatDouble(elem.get<double>());
        } else if (elem.is_boolean()) {
            oss << (elem.get<bool>() ? "true" : "false");
        } else if (elem.is_null()) {
            oss << "null";
        } else {
            oss << elem.dump();
        }
    }
    oss << "]";
    return oss.str();
}

std::string formatResultValue(const thrift::ResultValue& val) {
    switch (val.getType()) {
    case thrift::ResultValue::Type::bool_val:
        return val.get_bool_val() ? "true" : "false";

    case thrift::ResultValue::Type::int_val:
        return std::to_string(val.get_int_val());

    case thrift::ResultValue::Type::double_val:
        return formatDouble(val.get_double_val());

    case thrift::ResultValue::Type::string_val: {
        std::string escaped;
        for (char c : val.get_string_val()) {
            if (c == '\\')
                escaped += "\\\\";
            else if (c == '\'')
                escaped += "\\'";
            else
                escaped += c;
        }
        return "'" + escaped + "'";
    }

    case thrift::ResultValue::Type::vertex_json:
        return convertVertexJson(val.get_vertex_json());

    case thrift::ResultValue::Type::edge_json:
        return convertEdgeJson(val.get_edge_json());

    case thrift::ResultValue::Type::path_json:
        return val.get_path_json();

    case thrift::ResultValue::Type::list_json: {
        const auto& raw = val.get_list_json();
        try {
            auto arr = nlohmann::json::parse(raw);
            if (!arr.is_array())
                return raw;
            return formatJsonArray(arr);
        } catch (...) {
            return raw;
        }
    }

    case thrift::ResultValue::Type::map_json:
        return convertMapJson(val.get_map_json());

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
