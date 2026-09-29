#include "program/loader/csv_loader.hpp"

#include "common/types/temporal_value.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace eugraph {
namespace loader {

namespace {

using json = nlohmann::json;

// ==================== 类型解析 ====================

CsvColumnType parseTypeName(const std::string& raw) {
    static const std::unordered_map<std::string, CsvColumnType> kTypes = {
        {"BOOL", CsvColumnType::BOOL},
        {"INT", CsvColumnType::INT64},
        {"INT64", CsvColumnType::INT64},
        {"LONG", CsvColumnType::INT64},
        {"DOUBLE", CsvColumnType::DOUBLE},
        {"STRING", CsvColumnType::STRING},
        {"BOOL[]", CsvColumnType::BOOL_ARRAY},
        {"INT[]", CsvColumnType::INT64_ARRAY},
        {"INT64[]", CsvColumnType::INT64_ARRAY},
        {"LONG[]", CsvColumnType::INT64_ARRAY},
        {"DOUBLE[]", CsvColumnType::DOUBLE_ARRAY},
        {"STRING[]", CsvColumnType::STRING_ARRAY},
        {"DATE", CsvColumnType::DATE},
        {"DATETIME", CsvColumnType::DATETIME},
        {"DATETIME_WITH_TZ", CsvColumnType::DATETIME_WITH_TZ},
        {"TIME", CsvColumnType::TIME},
        {"TIME_WITH_TZ", CsvColumnType::TIME_WITH_TZ},
        {"DURATION", CsvColumnType::DURATION},
        {"DATE[]", CsvColumnType::DATE_ARRAY},
        {"DATETIME[]", CsvColumnType::DATETIME_ARRAY},
        {"TIME[]", CsvColumnType::TIME_ARRAY},
        {"DURATION[]", CsvColumnType::DURATION_ARRAY},
    };
    auto it = kTypes.find(raw);
    if (it == kTypes.end())
        throw std::runtime_error("unknown column type: '" + raw + "'");
    return it->second;
}

thrift_service::PropertyType toThriftType(CsvColumnType t) {
    switch (t) {
    case CsvColumnType::BOOL:
        return thrift_service::PropertyType::BOOL;
    case CsvColumnType::INT64:
        return thrift_service::PropertyType::INT64;
    case CsvColumnType::DOUBLE:
        return thrift_service::PropertyType::DOUBLE;
    case CsvColumnType::STRING:
        return thrift_service::PropertyType::STRING;
    case CsvColumnType::BOOL_ARRAY:
    case CsvColumnType::INT64_ARRAY:
        return thrift_service::PropertyType::INT64_ARRAY;
    case CsvColumnType::DOUBLE_ARRAY:
        return thrift_service::PropertyType::DOUBLE_ARRAY;
    case CsvColumnType::STRING_ARRAY:
        return thrift_service::PropertyType::STRING_ARRAY;
    case CsvColumnType::DATE:
    case CsvColumnType::DATETIME:
    case CsvColumnType::DATETIME_WITH_TZ:
    case CsvColumnType::DATE_ARRAY:
    case CsvColumnType::DATETIME_ARRAY:
        return thrift_service::PropertyType::DATETIME;
    case CsvColumnType::TIME:
    case CsvColumnType::TIME_WITH_TZ:
    case CsvColumnType::TIME_ARRAY:
        return thrift_service::PropertyType::TIME;
    case CsvColumnType::DURATION:
    case CsvColumnType::DURATION_ARRAY:
        return thrift_service::PropertyType::DURATION;
    }
    return thrift_service::PropertyType::STRING;
}

// ==================== 取值与转换 ====================

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        e--;
    return s.substr(b, e - b);
}

std::vector<std::string> splitArray(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ';'))
        out.push_back(item);
    return out;
}

[[noreturn]] void failValue(const std::string& file, int line, const PropertySpec& spec, const std::string& value,
                            const std::string& why) {
    throw std::runtime_error(file + ":" + std::to_string(line) + ": column '" +
                             (spec.header.empty() ? std::to_string(spec.index) : spec.header) + "' (property '" +
                             spec.name + "'): cannot parse '" + value + "' as " + why);
}

int64_t parseI64(const std::string& file, int line, const PropertySpec& spec, const std::string& raw) {
    try {
        size_t pos = 0;
        int64_t v = std::stoll(raw, &pos);
        if (pos != raw.size())
            failValue(file, line, spec, raw, "INT64");
        return v;
    } catch (const std::runtime_error&) {
        throw;
    } catch (...) {
        failValue(file, line, spec, raw, "INT64");
    }
}

double parseF64(const std::string& file, int line, const PropertySpec& spec, const std::string& raw) {
    try {
        size_t pos = 0;
        double v = std::stod(raw, &pos);
        if (pos != raw.size())
            failValue(file, line, spec, raw, "DOUBLE");
        return v;
    } catch (const std::runtime_error&) {
        throw;
    } catch (...) {
        failValue(file, line, spec, raw, "DOUBLE");
    }
}

DateTimeValue parseTemporal(const std::string& file, int line, const PropertySpec& spec, const std::string& raw,
                            DateTimeKind kind, const std::string& date_format) {
    // 纯数字按 epoch 解释（单位由 date_format 决定），否则按 ISO-8601 文本解析
    std::string t = trim(raw);
    bool numeric = !t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' || t[0] == '+');
    if (numeric) {
        for (char c : t) {
            if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '+') {
                numeric = false;
                break;
            }
        }
    }
    if (numeric && date_format != "iso") {
        int64_t epoch = parseI64(file, line, spec, t);
        int64_t seconds = epoch;
        int64_t nanos = 0;
        if (date_format == "epoch_ms") {
            seconds = epoch / 1000;
            nanos = (epoch % 1000) * 1000000;
            if (nanos < 0) {
                nanos += 1000000000;
                seconds -= 1;
            }
        } else if (date_format == "epoch_us") {
            seconds = epoch / 1000000;
            nanos = (epoch % 1000000) * 1000;
        } else if (date_format == "epoch_ns") {
            seconds = epoch / 1000000000;
            nanos = epoch % 1000000000;
        }
        auto dt = datetimeFromEpoch(seconds, nanos);
        dt.kind = kind;
        return dt;
    }
    return parseDatetimeStr(t, kind);
}

thrift_service::PropertyValueThrift toThriftValue(const std::string& file, int line, const PropertySpec& spec,
                                                  const std::string& value, const std::string& date_format) {
    thrift_service::PropertyValueThrift tv;
    if (value.empty())
        return tv; // 空值 = 不写入该属性

    auto set_dt = [&](const DateTimeValue& dt) {
        thrift_service::DateTimeValueThrift t;
        t.kind() = dt.kind == DateTimeKind::DATE
                       ? thrift_service::DateTimeKind::DATE
                       : (dt.kind == DateTimeKind::DATETIME ? thrift_service::DateTimeKind::DATETIME_WITH_TZ
                                                            : thrift_service::DateTimeKind::LOCAL_DATETIME);
        t.year() = dt.year;
        t.month() = dt.month;
        t.day() = dt.day;
        t.hour() = dt.hour;
        t.minute() = dt.minute;
        t.second() = dt.second;
        t.nanos() = dt.nanos;
        t.tz_offset_min() = dt.tz_offset_sec / 60;
        t.tz_name() = tzNameOrEmpty(dt.tz_name);
        tv.set_datetime_val(std::move(t));
    };

    switch (spec.type) {
    case CsvColumnType::BOOL:
        if (value != "true" && value != "false")
            failValue(file, line, spec, value, "BOOL");
        tv.set_bool_val(value == "true");
        break;
    case CsvColumnType::INT64:
        tv.set_int_val(parseI64(file, line, spec, value));
        break;
    case CsvColumnType::DOUBLE:
        tv.set_double_val(parseF64(file, line, spec, value));
        break;
    case CsvColumnType::STRING:
        tv.set_string_val(value);
        break;
    case CsvColumnType::BOOL_ARRAY:
    case CsvColumnType::INT64_ARRAY: {
        std::vector<int64_t> arr;
        for (const auto& s : splitArray(value))
            arr.push_back(parseI64(file, line, spec, s));
        tv.set_int_array(std::move(arr));
        break;
    }
    case CsvColumnType::DOUBLE_ARRAY: {
        std::vector<double> arr;
        for (const auto& s : splitArray(value))
            arr.push_back(parseF64(file, line, spec, s));
        tv.set_double_array(std::move(arr));
        break;
    }
    case CsvColumnType::STRING_ARRAY:
        tv.set_string_array(splitArray(value));
        break;
    case CsvColumnType::DATE:
        set_dt(parseTemporal(file, line, spec, value, DateTimeKind::DATE, date_format));
        break;
    case CsvColumnType::DATETIME:
        set_dt(parseTemporal(file, line, spec, value, DateTimeKind::LOCAL_DATETIME, date_format));
        break;
    case CsvColumnType::DATETIME_WITH_TZ:
        set_dt(parseTemporal(file, line, spec, value, DateTimeKind::DATETIME, date_format));
        break;
    case CsvColumnType::TIME: {
        auto tv2 = parseTimeStr(value, TimeKind::LOCAL_TIME);
        thrift_service::TimeValueThrift t;
        t.kind() = thrift_service::TimeKind::LOCAL_TIME;
        t.hour() = tv2.hour;
        t.minute() = tv2.minute;
        t.second() = tv2.second;
        t.nanos() = tv2.nanos;
        tv.set_time_val(std::move(t));
        break;
    }
    case CsvColumnType::TIME_WITH_TZ: {
        auto tv2 = parseTimeStr(value, TimeKind::TIME);
        thrift_service::TimeValueThrift t;
        t.kind() = thrift_service::TimeKind::TIME_WITH_TZ;
        t.hour() = tv2.hour;
        t.minute() = tv2.minute;
        t.second() = tv2.second;
        t.nanos() = tv2.nanos;
        t.tz_offset_min() = tv2.tz_offset_sec / 60;
        t.tz_name() = tzNameOrEmpty(tv2.tz_name);
        tv.set_time_val(std::move(t));
        break;
    }
    case CsvColumnType::DURATION: {
        auto dv = parseDurationFromString(value);
        thrift_service::DurationValueThrift d;
        d.months() = dv.months;
        d.days() = dv.days;
        d.seconds() = dv.seconds;
        d.nanos() = dv.nanos;
        tv.set_duration_val(std::move(d));
        break;
    }
    case CsvColumnType::DATE_ARRAY:
    case CsvColumnType::DATETIME_ARRAY: {
        std::vector<thrift_service::DateTimeValueThrift> arr;
        for (const auto& s : splitArray(value)) {
            PropertySpec one = spec;
            thrift_service::PropertyValueThrift single;
            DateTimeKind kind =
                spec.type == CsvColumnType::DATE_ARRAY ? DateTimeKind::DATE : DateTimeKind::LOCAL_DATETIME;
            auto dt = parseTemporal(file, line, one, s, kind, date_format);
            thrift_service::DateTimeValueThrift t;
            t.kind() = kind == DateTimeKind::DATE ? thrift_service::DateTimeKind::DATE
                                                  : thrift_service::DateTimeKind::LOCAL_DATETIME;
            t.year() = dt.year;
            t.month() = dt.month;
            t.day() = dt.day;
            t.hour() = dt.hour;
            t.minute() = dt.minute;
            t.second() = dt.second;
            t.nanos() = dt.nanos;
            arr.push_back(std::move(t));
        }
        tv.set_datetime_array(std::move(arr));
        break;
    }
    case CsvColumnType::TIME_ARRAY: {
        std::vector<thrift_service::TimeValueThrift> arr;
        for (const auto& s : splitArray(value)) {
            auto tv2 = parseTimeStr(s, TimeKind::LOCAL_TIME);
            thrift_service::TimeValueThrift t;
            t.kind() = thrift_service::TimeKind::LOCAL_TIME;
            t.hour() = tv2.hour;
            t.minute() = tv2.minute;
            t.second() = tv2.second;
            t.nanos() = tv2.nanos;
            arr.push_back(std::move(t));
        }
        tv.set_time_array(std::move(arr));
        break;
    }
    case CsvColumnType::DURATION_ARRAY: {
        std::vector<thrift_service::DurationValueThrift> arr;
        for (const auto& s : splitArray(value)) {
            auto dv = parseDurationFromString(s);
            thrift_service::DurationValueThrift d;
            d.months() = dv.months;
            d.days() = dv.days;
            d.seconds() = dv.seconds;
            d.nanos() = dv.nanos;
            arr.push_back(std::move(d));
        }
        tv.set_duration_array(std::move(arr));
        break;
    }
    }
    return tv;
}

// ==================== schema 解析辅助 ====================

std::string reqString(const json& obj, const char* key, const std::string& where) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        throw std::runtime_error(where + ": missing or non-string field '" + key + "'");
    return it->get<std::string>();
}

/// 解析一个「列声明」：
///   "TYPE"                     → 属性名 = 键，来源列 = 键
///   {"type": "TYPE"}           → 同上
///   {"header": "列名", ...}    → 来源列 = header，属性名 = 键
///   {"index": n, ...}          → 来源列 = 第 n 列
/// key_is_property = true（columns）时键是属性名；false（pk/src/dst）时键是列名。
PropertySpec parseColumnSpec(const std::string& key, const json& value, bool key_is_property,
                             const std::string& where) {
    PropertySpec spec;
    if (value.is_string()) {
        spec.type = parseTypeName(value.get<std::string>());
        spec.type_declared = true;
        if (key_is_property) {
            spec.name = key;
            spec.header = key;
        } else {
            spec.name = key;
            spec.header = key;
        }
        return spec;
    }
    if (!value.is_object())
        throw std::runtime_error(where + ": column '" + key + "' must be a type string or an object");

    const std::string header = value.contains("header") ? reqString(value, "header", where) : std::string{};
    const int index = value.contains("index") ? value["index"].get<int>() : -1;
    if (!header.empty() && index >= 0)
        throw std::runtime_error(where + ": column '" + key + "' specifies both header and index");
    const std::string name = value.contains("name") ? reqString(value, "name", where) : key;

    spec.name = name;
    spec.header = header;
    spec.index = index;
    if (value.contains("type")) {
        spec.type = parseTypeName(value["type"].get<std::string>());
        spec.type_declared = true;
    }
    return spec;
}

/// pk 元素：字符串（列名，属性名同）或 {列名: {name?, type?}} 或 {header/name/type}
PropertySpec parsePkElement(const json& element, const std::string& where, CsvColumnType* explicit_type,
                            bool* type_declared) {
    if (element.is_string()) {
        PropertySpec spec;
        spec.header = element.get<std::string>();
        spec.name = spec.header;
        if (type_declared != nullptr)
            *type_declared = false;
        return spec;
    }
    if (!element.is_object() || element.size() != 1)
        throw std::runtime_error(where + ": pk element must be a column name string or a single-key object");
    auto it = element.begin();
    const std::string key = it.key();
    const json& v = it.value();
    if (v.is_object()) {
        PropertySpec spec = parseColumnSpec(key, v, /*key_is_property=*/false, where);
        bool declared = v.contains("type");
        if (declared) {
            spec.type = parseTypeName(v["type"].get<std::string>());
            spec.type_declared = true;
        }
        if (explicit_type != nullptr)
            *explicit_type = spec.type;
        if (type_declared != nullptr)
            *type_declared = declared;
        return spec;
    }
    throw std::runtime_error(where + ": pk element '" + key + "' must map to an object");
}

std::string applyCase(const std::string& value, const std::string& mode) {
    if (mode == "none" || mode.empty())
        return value;
    std::string out = value;
    if (mode == "capitalize") {
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(out[i]))
                                              : std::tolower(static_cast<unsigned char>(out[i])));
        }
    } else if (mode == "lower") {
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
        throw std::runtime_error("unknown case mode: '" + mode + "' (expected none|capitalize|lower)");
    }
    return out;
}

int resolveColumn(const PropertySpec& spec, const std::vector<std::string>& header, const std::string& where,
                  bool required) {
    if (!spec.header.empty()) {
        for (size_t i = 0; i < header.size(); ++i) {
            if (header[i] == spec.header)
                return static_cast<int>(i);
        }
        std::string available;
        for (const auto& h : header)
            available += (available.empty() ? "" : ", ") + h;
        throw std::runtime_error(where + ": column '" + spec.header + "' not found in header [" + available + "]");
    }
    if (spec.index >= 0) {
        if (spec.index >= static_cast<int>(header.size()))
            throw std::runtime_error(where + ": column index " + std::to_string(spec.index) +
                                     " out of range (header has " + std::to_string(header.size()) + " columns)");
        return spec.index;
    }
    if (required)
        throw std::runtime_error(where + ": column '" + spec.name + "' needs header or index");
    return -1;
}

} // namespace

// ==================== schema 装载 ====================

SchemaConfig loadSchemaConfig(const std::filesystem::path& schema_path, const std::filesystem::path& data_dir,
                              const CsvDialect& dialect, bool strict_types, const CliOverrides* overrides) {
    SchemaConfig cfg;
    cfg.schema_path = schema_path;
    cfg.data_dir = data_dir;

    std::ifstream ifs(schema_path);
    if (!ifs)
        throw std::runtime_error("cannot open schema file: " + schema_path.string());
    json doc;
    try {
        doc = json::parse(ifs);
    } catch (const json::exception& e) {
        throw std::runtime_error("schema file is not valid JSON (" + schema_path.string() + "): " + e.what());
    }
    if (!doc.is_object())
        throw std::runtime_error("schema root must be an object: " + schema_path.string());
    if (doc.contains("ignore")) {
        if (!doc["ignore"].is_array())
            throw std::runtime_error("schema: 'ignore' must be an array of file paths");
        for (const auto& item : doc["ignore"]) {
            if (!item.is_string())
                throw std::runtime_error("schema: 'ignore' entries must be strings");
            cfg.ignored_files.push_back(item.get<std::string>());
        }
    }
    if (doc.contains("date_format")) {
        cfg.date_format = reqString(doc, "date_format", "schema");
        static const std::set<std::string> kFormats = {"epoch_ms", "epoch_s", "epoch_us", "epoch_ns", "iso"};
        if (!kFormats.count(cfg.date_format))
            throw std::runtime_error("schema: unknown date_format '" + cfg.date_format + "'");
    }

    auto resolve_file = [&](const std::string& rel) {
        std::filesystem::path p(rel);
        return p.is_absolute() ? p : data_dir / p;
    };

    // 先解析 labels（顶点），再解析 relationships（边；端点标签要校验）
    if (!doc.contains("labels") || !doc["labels"].is_object())
        throw std::runtime_error("schema: missing object field 'labels' (map of label -> files)");
    if (!doc.contains("relationships") || !doc["relationships"].is_object())
        throw std::runtime_error("schema: missing object field 'relationships' (map of edge type -> files)");

    for (auto label_it = doc["labels"].begin(); label_it != doc["labels"].end(); ++label_it) {
        const std::string label = label_it.key();
        if (!label_it.value().is_array() || label_it.value().empty())
            throw std::runtime_error("schema: labels." + label + " must be a non-empty array of files");
        for (const auto& entry : label_it.value()) {
            const std::string where = "labels." + label + "[" + entry.dump().substr(0, 60) + "]";
            if (!entry.is_object())
                throw std::runtime_error("schema: " + where + " must be an object");
            FileSpec fs;
            fs.element = label;
            fs.declared_file = reqString(entry, "file", where);
            fs.path = resolve_file(fs.declared_file);
            if (!std::filesystem::exists(fs.path))
                throw std::runtime_error("schema: " + where + ": data file not found: " + fs.path.string());

            if (entry.contains("columns")) {
                const auto& cols = entry["columns"];
                if (!cols.is_object())
                    throw std::runtime_error("schema: " + where + ".columns must be an object (property name -> spec)");
                for (auto c = cols.begin(); c != cols.end(); ++c)
                    fs.columns.push_back(parseColumnSpec(c.key(), c.value(), /*key_is_property=*/true, where));
            }

            if (entry.contains("pk")) {
                const auto& pk = entry["pk"];
                std::vector<json> elems;
                if (pk.is_string() || pk.is_object())
                    elems.push_back(pk);
                else if (pk.is_array())
                    elems = pk.get<std::vector<json>>();
                else
                    throw std::runtime_error("schema: " + where + ".pk must be a string, object, or array");
                if (elems.empty())
                    throw std::runtime_error("schema: " + where + ".pk must not be empty");
                for (const auto& e : elems) {
                    CsvColumnType t = CsvColumnType::INT64;
                    bool declared = false;
                    PropertySpec spec = parsePkElement(e, where + ".pk", &t, &declared);
                    spec.type_declared = declared;
                    if (declared)
                        spec.type = t;
                    fs.pk.push_back(spec);
                }
                fs.has_pk = true;
            }

            if (entry.contains("label")) {
                const auto& lj = entry["label"];
                LabelSource src;
                if (!lj.is_object())
                    throw std::runtime_error("schema: " + where + ".label must be an object");
                if (lj.contains("derived")) {
                    src.derived = lj["derived"].get<std::vector<std::string>>();
                } else {
                    if (lj.contains("header"))
                        src.header = reqString(lj, "header", where + ".label");
                    if (lj.contains("column"))
                        src.index = lj["column"].get<int>();
                    if (src.header.empty() && src.index < 0)
                        throw std::runtime_error("schema: " + where + ".label needs header, column, or derived");
                }
                if (lj.contains("case"))
                    src.case_mode = reqString(lj, "case", where + ".label");
                fs.row_label = std::move(src);
            }
            cfg.vertex_files.push_back(std::move(fs));
        }
    }

    for (auto rel_it = doc["relationships"].begin(); rel_it != doc["relationships"].end(); ++rel_it) {
        const std::string etype = rel_it.key();
        if (!rel_it.value().is_array() || rel_it.value().empty())
            throw std::runtime_error("schema: relationships." + etype + " must be a non-empty array of files");
        for (const auto& entry : rel_it.value()) {
            const std::string where = "relationships." + etype;
            if (!entry.is_object())
                throw std::runtime_error("schema: " + where + " entry must be an object");
            FileSpec fs;
            fs.element = etype;
            fs.declared_file = reqString(entry, "file", where);
            fs.path = resolve_file(fs.declared_file);
            if (!std::filesystem::exists(fs.path))
                throw std::runtime_error("schema: " + where + ": data file not found: " + fs.path.string());

            // src/dst：字符串（列名，属性名同）或 {列名: {...}} / {header: ...}
            auto parse_endpoint = [&](const char* key) {
                if (!entry.contains(key))
                    throw std::runtime_error("schema: " + where + " (" + fs.declared_file + "): missing '" + key + "'");
                const auto& v = entry[key];
                PropertySpec spec;
                std::string ep_label;
                if (v.is_string()) {
                    spec.header = v.get<std::string>();
                    spec.name = spec.header;
                } else if (v.is_object() && v.size() == 1 && !v.contains("header")) {
                    auto it = v.begin();
                    CsvColumnType t = CsvColumnType::INT64;
                    bool declared = false;
                    spec = parsePkElement(v, where + "." + key, &t, &declared);
                    if (v.begin().value().is_object() && v.begin().value().contains("label"))
                        ep_label = reqString(v.begin().value(), "label", where);
                    if (declared)
                        spec.type = t;
                } else if (v.is_object()) {
                    spec = parseColumnSpec(std::string(key), v, /*key_is_property=*/false, where);
                    if (v.contains("label") && v["label"].is_string())
                        ep_label = v["label"].get<std::string>();
                } else {
                    throw std::runtime_error("schema: " + where + "." + key + " must be a string or object");
                }
                return std::make_pair(spec, ep_label);
            };

            auto parse_endpoint_list = [&](const char* key) {
                std::vector<PropertySpec> specs;
                std::string ep_label;
                if (entry.contains(key) && entry[key].is_array()) {
                    for (const auto& e : entry[key]) {
                        CsvColumnType t = CsvColumnType::INT64;
                        bool declared = false;
                        specs.push_back(parsePkElement(e, where + "." + key, &t, &declared));
                    }
                    return std::make_pair(specs, ep_label);
                }
                auto [spec, lbl] = parse_endpoint(key);
                specs.push_back(std::move(spec));
                return std::make_pair(specs, lbl);
            };
            auto [src_specs, src_ep_label] = parse_endpoint_list("src");
            auto [dst_specs, dst_ep_label] = parse_endpoint_list("dst");
            fs.src = std::move(src_specs);
            fs.dst = std::move(dst_specs);

            // 端点标签：顶层 src_label/dst_label 优先，等价写法在端点对象里
            auto label_from_field = [&](const char* label_key, const std::string& inline_label) {
                std::string top;
                if (entry.contains(label_key))
                    top = reqString(entry, label_key, where);
                if (!top.empty() && !inline_label.empty() && top != inline_label)
                    throw std::runtime_error("schema: " + where + ": '" + std::string(label_key) +
                                             "' and inline label disagree");
                return top.empty() ? inline_label : top;
            };
            fs.src_label = label_from_field("src_label", src_ep_label);
            fs.dst_label = label_from_field("dst_label", dst_ep_label);
            if (fs.src_label.empty() || fs.dst_label.empty())
                throw std::runtime_error("schema: " + where + " (" + fs.declared_file +
                                         "): src_label/dst_label are required (endpoint labels are not inferred)");

            if (entry.contains("columns")) {
                const auto& cols = entry["columns"];
                if (!cols.is_object())
                    throw std::runtime_error("schema: " + where + ".columns must be an object");
                for (auto c = cols.begin(); c != cols.end(); ++c)
                    fs.columns.push_back(parseColumnSpec(c.key(), c.value(), /*key_is_property=*/true, where));
            }
            cfg.edge_files.push_back(std::move(fs));
        }
    }

    // ---- CLI 覆盖：date_format / types / pk ----
    if (overrides != nullptr) {
        if (overrides->date_format.has_value())
            cfg.date_format = *overrides->date_format;
        auto apply_types = [&](std::vector<FileSpec>& files, const std::string& element_name,
                               const std::unordered_map<std::string, std::string>& type_map) {
            for (auto& fs : files) {
                if (fs.element != element_name)
                    continue;
                for (const auto& [prop, type_name] : type_map) {
                    CsvColumnType t = parseTypeName(type_name);
                    auto it = std::find_if(fs.columns.begin(), fs.columns.end(),
                                           [&](const PropertySpec& c) { return c.name == prop; });
                    if (it != fs.columns.end()) {
                        it->type = t;
                        it->type_declared = true;
                    } else {
                        // 未声明过：按「属性名 = 列名」新增（需要表头里真的有这一列）
                        PropertySpec spec;
                        spec.name = prop;
                        spec.header = prop;
                        spec.type = t;
                        spec.type_declared = true;
                        fs.columns.push_back(std::move(spec));
                    }
                }
            }
        };
        for (const auto& fs : cfg.vertex_files) {
            auto it = overrides->types.find(fs.element);
            if (it != overrides->types.end())
                apply_types(cfg.vertex_files, fs.element, it->second);
        }
        for (const auto& fs : cfg.edge_files) {
            auto it = overrides->types.find(fs.element);
            if (it != overrides->types.end())
                apply_types(cfg.edge_files, fs.element, it->second);
        }
        for (const auto& [label, pk_names] : overrides->pk) {
            bool found = false;
            for (auto& fs : cfg.vertex_files) {
                if (fs.element != label)
                    continue;
                found = true;
                fs.pk.clear();
                for (const auto& n : pk_names) {
                    PropertySpec spec;
                    spec.name = n;
                    spec.header = n;
                    fs.pk.push_back(std::move(spec));
                }
                fs.has_pk = true;
            }
            if (!found)
                throw std::runtime_error("--pk: label '" + label + "' is not declared in the schema");
        }
    }

    // ---- 非 strict：对未在 columns 声明的列按采样推断补上类型 ----
    if (!strict_types) {
        auto infer_missing = [&](std::vector<FileSpec>& files, bool is_edge) {
            for (auto& fs : files) {
                CsvFile csv = readCsvFile(fs.path, dialect);
                std::set<std::string> declared;
                for (const auto& c : fs.columns)
                    declared.insert(c.header.empty() ? c.name : c.header);
                for (const auto& rl : std::vector<std::optional<LabelSource>>{fs.row_label}) {
                    if (rl.has_value() && !rl->header.empty())
                        declared.insert(rl->header);
                }
                bool has_dup_header = false;
                {
                    std::set<std::string> seen;
                    for (const auto& h : csv.header)
                        if (!seen.insert(h).second)
                            has_dup_header = true;
                }
                for (size_t i = 0; i < csv.header.size(); ++i) {
                    const std::string& h = csv.header[i];
                    if (declared.count(h))
                        continue;
                    if (is_edge) {
                        bool is_endpoint = false;
                        for (const auto& c : fs.src)
                            if ((c.header.empty() ? c.name : c.header) == h)
                                is_endpoint = true;
                        for (const auto& c : fs.dst)
                            if ((c.header.empty() ? c.name : c.header) == h)
                                is_endpoint = true;
                        if (is_endpoint)
                            continue;
                    }
                    if (has_dup_header) {
                        spdlog::warn("[loader] {}: duplicate header '{}' left undeclared; skipped in non-strict "
                                     "inference (declare it with header/index)",
                                     fs.declared_file, h);
                        continue;
                    }
                    bool all_int = true;
                    for (const auto& row : csv.rows) {
                        if (i >= row.fields.size() || row.fields[i].empty())
                            continue;
                        size_t pos = 0;
                        try {
                            std::stoll(row.fields[i], &pos);
                        } catch (...) {
                            all_int = false;
                            break;
                        }
                        if (pos != row.fields[i].size()) {
                            all_int = false;
                            break;
                        }
                    }
                    PropertySpec spec;
                    spec.name = h;
                    spec.header = h;
                    spec.type = all_int ? CsvColumnType::INT64 : CsvColumnType::STRING;
                    spec.type_declared = false;
                    spdlog::info("[loader] {}: column '{}' not declared; inferred {}", fs.declared_file, h,
                                 all_int ? "INT64" : "STRING");
                    fs.columns.push_back(std::move(spec));
                }
            }
        };
        infer_missing(cfg.vertex_files, /*is_edge=*/false);
        infer_missing(cfg.edge_files, /*is_edge=*/true);
    }

    // 合并每个标签的属性（按属性名去重，保留首次出现顺序），并校验 pk 引用
    for (const auto& fs : cfg.vertex_files) {
        auto& merged = cfg.merged_properties[fs.element];
        for (const auto& c : fs.columns) {
            auto it =
                std::find_if(merged.begin(), merged.end(), [&](const PropertySpec& p) { return p.name == c.name; });
            if (it == merged.end()) {
                merged.push_back(c);
            } else if (it->type != c.type) {
                throw std::runtime_error("schema: property '" + c.name + "' on label '" + fs.element +
                                         "' declared with conflicting types");
            }
        }
        for (const auto& pk_spec : fs.pk) {
            auto it = std::find_if(merged.begin(), merged.end(),
                                   [&](const PropertySpec& p) { return p.name == pk_spec.name; });
            if (it == merged.end())
                throw std::runtime_error("schema: label '" + fs.element + "' (" + fs.declared_file +
                                         "): primary key '" + pk_spec.name + "' is not declared in columns");
        }
    }

    // ---- strict：未声明的列必须报错（指出文件与列名），而不是静默丢弃 ----
    if (strict_types) {
        auto check_declared = [&](const FileSpec& fs, bool is_edge) {
            CsvFile csv = readCsvFile(fs.path, dialect);
            std::set<std::string> declared;
            for (const auto& c : fs.columns)
                declared.insert(c.header.empty() ? (c.index >= 0 && c.index < static_cast<int>(csv.header.size())
                                                        ? csv.header[static_cast<size_t>(c.index)]
                                                        : c.name)
                                                 : c.header);
            if (fs.row_label.has_value() && !fs.row_label->header.empty())
                declared.insert(fs.row_label->header);
            for (const auto& c : fs.pk)
                declared.insert(c.header.empty() ? c.name : c.header);
            if (is_edge) {
                for (const auto& c : fs.src)
                    declared.insert(c.header.empty() ? c.name : c.header);
                for (const auto& c : fs.dst)
                    declared.insert(c.header.empty() ? c.name : c.header);
            }
            std::vector<std::string> missing;
            for (const auto& h : csv.header) {
                if (!declared.count(h))
                    missing.push_back(h);
            }
            if (!missing.empty()) {
                std::string list;
                for (const auto& m : missing)
                    list += (list.empty() ? "" : ", ") + m;
                throw std::runtime_error("schema: " + fs.declared_file + ": column(s) not declared: [" + list +
                                         "]. Declare them in columns (property name -> header), or pass "
                                         "--no-schema-strict to infer their types");
            }
        };
        for (const auto& fs : cfg.vertex_files)
            check_declared(fs, /*is_edge=*/false);
        for (const auto& fs : cfg.edge_files)
            check_declared(fs, /*is_edge=*/true);
    }

    // 标签集合与主键声明（供边端点校验）
    std::unordered_map<std::string, bool> label_has_pk;
    for (const auto& fs : cfg.vertex_files) {
        if (fs.has_pk)
            label_has_pk[fs.element] = true;
        else
            label_has_pk.emplace(fs.element, false);
    }
    for (const auto& fs : cfg.edge_files) {
        for (const auto& [role, lbl] :
             std::vector<std::pair<std::string, std::string>>{{"src", fs.src_label}, {"dst", fs.dst_label}}) {
            auto it = label_has_pk.find(lbl);
            if (it == label_has_pk.end())
                throw std::runtime_error("schema: relationships." + fs.element + " (" + fs.declared_file +
                                         "): " + role + "_label '" + lbl + "' is not declared under labels");
            if (!it->second)
                throw std::runtime_error("schema: relationships." + fs.element + " (" + fs.declared_file +
                                         "): " + role + "_label '" + lbl +
                                         "' declares no primary key, so its vertices cannot be referenced");
        }
    }

    spdlog::info("[loader] schema: {} label file(s) across {} label(s), {} edge file(s) across {} type(s)",
                 cfg.vertex_files.size(), cfg.merged_properties.size(), cfg.edge_files.size(), [&] {
                     std::set<std::string> t;
                     for (const auto& f : cfg.edge_files)
                         t.insert(f.element);
                     return t.size();
                 }());
    return cfg;
}

// ==================== DDL ====================

namespace {

std::vector<thrift_service::PropertyDefThrift> toThriftProps(const std::vector<PropertySpec>& specs) {
    std::vector<thrift_service::PropertyDefThrift> out;
    out.reserve(specs.size());
    for (const auto& p : specs) {
        thrift_service::PropertyDefThrift pd;
        pd.name() = p.name;
        pd.type() = toThriftType(p.type);
        pd.is_required() = false;
        out.push_back(std::move(pd));
    }
    return out;
}

/// 所有标签（保持稳定顺序，便于日志与回放）
std::vector<std::string> allLabels(const SchemaConfig& config) {
    std::vector<std::string> labels;
    for (const auto& f : config.vertex_files) {
        if (std::find(labels.begin(), labels.end(), f.element) == labels.end())
            labels.push_back(f.element);
    }
    return labels;
}

std::vector<std::string> allEdgeTypes(const SchemaConfig& config) {
    std::vector<std::string> types;
    for (const auto& f : config.edge_files) {
        if (std::find(types.begin(), types.end(), f.element) == types.end())
            types.push_back(f.element);
    }
    return types;
}

/// 该标签的主键属性名（按元组顺序）。多个文件声明同一标签时取第一个声明了主键的文件。
std::vector<std::string> labelPrimaryKeyNames(const SchemaConfig& config, const std::string& label) {
    for (const auto& f : config.vertex_files) {
        if (f.element != label || !f.has_pk)
            continue;
        std::vector<std::string> names;
        for (const auto& p : f.pk)
            names.push_back(p.name);
        return names;
    }
    return {};
}

} // namespace

void createLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config) {
    for (const auto& label : allLabels(config)) {
        auto props = toThriftProps(config.merged_properties.at(label));
        auto pk = labelPrimaryKeyNames(config, label);
        spdlog::info("[loader] Creating label '{}' with {} properties, pk=[{}]", label, props.size(), [&] {
            std::string s;
            for (const auto& n : pk)
                s += (s.empty() ? "" : ",") + n;
            return s;
        }());
        // 标签已存在时的两种情形：
        //   ① 声明一致（幂等重跑）→ 直接复用；
        //   ② 属性集不同（同一标签由多个文件供给，如 Comment/Post 共享 Message）→ 增量合并。
        // 主键声明不一致由服务端报错（不允许改主键定义），这里不吞异常。
        try {
            client.createLabel(label, props, "default", pk);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("already exists") == std::string::npos)
                throw;
            spdlog::info("[loader] label '{}' exists; merging {} declared properties", label, props.size());
            client.createLabel(label, props, "default", pk, props);
        }
    }
}

void createEdgeLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config) {
    for (const auto& type : allEdgeTypes(config)) {
        // 同一边类型的多个文件：属性按名合并
        std::vector<PropertySpec> merged;
        for (const auto& f : config.edge_files) {
            if (f.element != type)
                continue;
            for (const auto& c : f.columns) {
                auto it =
                    std::find_if(merged.begin(), merged.end(), [&](const PropertySpec& p) { return p.name == c.name; });
                if (it == merged.end())
                    merged.push_back(c);
                else if (it->type != c.type)
                    throw std::runtime_error("schema: edge property '" + c.name + "' on type '" + type +
                                             "' declared with conflicting types");
            }
        }
        auto props = toThriftProps(merged);
        spdlog::info("[loader] Creating edge label '{}' with {} properties", type, props.size());
        client.createEdgeLabel(type, props, "default");
    }
}

void createPrimaryKeyIndexes(shell::EuGraphRpcClient& client, const SchemaConfig& config) {
    for (const auto& label : allLabels(config)) {
        auto pk = labelPrimaryKeyNames(config, label);
        if (pk.empty())
            continue;
        std::string index_name = "idx_" + label;
        std::string on;
        for (const auto& name : pk) {
            index_name += "_" + name;
            if (!on.empty())
                on += ", ";
            on += "n." + name;
        }
        index_name += "_unique";
        // 先建索引：批量写入会在同一事务内维护它，因此装载期索引始终可用
        std::string query = "CREATE UNIQUE INDEX " + index_name + " FOR (n:" + label + ") ON (" + on + ")";
        try {
            spdlog::info("[loader] {}", query);
            client.executeCypher(query, "default");
        } catch (const std::exception& e) {
            // 已存在（幂等重跑）继续；其它错误上抛
            std::string msg = e.what();
            if (msg.find("already exists") == std::string::npos)
                throw;
            spdlog::info("[loader] unique index '{}' already exists, reusing", index_name);
        }
    }
}

// ==================== 装载 ====================

namespace {

template <typename FileFn>
void runFilesParallel(const std::vector<shell::EuGraphRpcClient*>& clients, size_t file_count, int concurrency,
                      FileFn&& fn) {
    if (file_count == 0 || clients.empty())
        return;
    if (concurrency <= 1 || file_count == 1) {
        for (size_t i = 0; i < file_count; ++i)
            fn(*clients[0], i);
        return;
    }
    int workers = std::min<int>(concurrency, static_cast<int>(file_count));
    std::atomic<size_t> next_index{0};
    std::mutex error_mu;
    std::exception_ptr error;
    auto worker = [&](int worker_id) {
        shell::EuGraphRpcClient& client = *clients[static_cast<size_t>(worker_id) % clients.size()];
        while (true) {
            size_t index = next_index.fetch_add(1);
            if (index >= file_count)
                break;
            try {
                fn(client, index);
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_mu);
                if (!error)
                    error = std::current_exception();
                break;
            }
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i)
        threads.emplace_back(worker, i);
    for (auto& t : threads)
        t.join();
    if (error)
        std::rethrow_exception(error);
}

/// 按 (标签, 主键属性名列表) 排序，保证批量写入时同标签的索引条目按声明顺序组织。
struct PkKey {
    std::string label;
    std::vector<std::string> names;
    bool operator<(const PkKey& o) const {
        return label < o.label;
    }
};

/// 行级标签（label.header / derived）在装载前必须先建好标签，否则批量写入时
/// 服务端会报 "Label not found"。属性定义跟随主标签：这些标签下的顶点由同一批
/// CSV 列供给，属性与主标签一致（主标签负责索引，行级标签用于按名字查询）。
void createRowLabels(shell::EuGraphRpcClient& client, const FileSpec& fs, const SchemaConfig& config,
                     const CsvDialect& dialect) {
    if (!fs.row_label.has_value())
        return;
    const auto& merged = config.merged_properties.at(fs.element);

    std::vector<std::string> names;
    if (!fs.row_label->derived.empty()) {
        for (const auto& d : fs.row_label->derived) {
            if (d != fs.element)
                names.push_back(d);
        }
    } else {
        CsvFile csv = readCsvFile(fs.path, dialect);
        PropertySpec tmp;
        tmp.header = fs.row_label->header;
        tmp.index = fs.row_label->index;
        tmp.name = "__row_label__";
        int col = resolveColumn(tmp, csv.header, fs.declared_file + " (label)", /*required=*/true);
        std::set<std::string> seen;
        for (const auto& row : csv.rows) {
            if (col >= static_cast<int>(row.fields.size()))
                continue;
            std::string v = applyCase(trim(row.fields[static_cast<size_t>(col)]), fs.row_label->case_mode);
            if (v.empty() || v == fs.element)
                continue;
            if (seen.insert(v).second)
                names.push_back(v);
        }
    }
    for (const auto& name : names) {
        auto props = toThriftProps(merged);
        spdlog::info("[loader] Creating row label '{}' (same properties as '{}')", name, fs.element);
        try {
            client.createLabel(name, props, "default", /*pk_props=*/{});
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("already exists") == std::string::npos)
                throw;
            // 同一行级标签可能由多个主标签供给（Comment/Post → Message）：属性取并集
            spdlog::info("[loader] row label '{}' exists; merging {} properties", name, props.size());
            client.createLabel(name, props, "default", /*pk_props=*/{}, props);
        }
    }
}

void loadOneVertexFile(shell::EuGraphRpcClient& client, const FileSpec& fs, const SchemaConfig& config,
                       const CsvDialect& dialect, int batch_size, int64_t& written, int64_t& duplicates) {
    CsvFile csv = readCsvFile(fs.path, dialect);

    // 解析列号（属性 / 主键 / 行级标签）
    auto props = fs.columns;
    const std::string where = fs.declared_file;
    for (auto& p : props)
        p.column = resolveColumn(p, csv.header, where + " (properties)", /*required=*/true);

    std::vector<PropertySpec> pk = fs.pk;
    for (auto& p : pk) {
        // 主键元素的键是列名：属性名缺省 = 列名，类型必须是 columns 里声明的类型
        const PropertySpec* declared = nullptr;
        for (const auto& c : fs.columns) {
            if (c.name == p.name) {
                declared = &c;
                break;
            }
        }
        if (declared == nullptr)
            throw std::runtime_error("schema: " + where + ": primary key property '" + p.name +
                                     "' is not declared in columns");
        if (!declared->type_declared)
            throw std::runtime_error("schema: " + where + ": primary key property '" + p.name +
                                     "' must declare a type in columns (primary key types are never inferred)");
        PropertySpec resolved = *declared;
        resolved.column = resolveColumn(resolved, csv.header, where + " (primary key)", /*required=*/true);
        p = resolved;
    }

    // derived 模式：整份文件共享同一组额外标签
    std::vector<std::string> extra_labels;
    if (fs.row_label.has_value() && !fs.row_label->derived.empty()) {
        for (const auto& d : fs.row_label->derived) {
            if (d != fs.element)
                extra_labels.push_back(d);
        }
    }

    int row_label_col = -1;
    if (fs.row_label.has_value()) {
        const auto& rl = *fs.row_label;
        if (rl.derived.empty()) {
            PropertySpec tmp;
            tmp.header = rl.header;
            tmp.index = rl.index;
            tmp.name = "__row_label__";
            row_label_col = resolveColumn(tmp, csv.header, where + " (label)", /*required=*/true);
        }
    }

    // 属性按该标签合并后的顺序排列，保证不同文件的列顺序不影响结果
    const auto& merged = config.merged_properties.at(fs.element);
    std::vector<int> prop_col_for_merged(merged.size(), -1);
    for (size_t mi = 0; mi < merged.size(); ++mi) {
        for (const auto& c : props) {
            if (c.name == merged[mi].name) {
                prop_col_for_merged[mi] = c.column;
                break;
            }
        }
    }

    std::vector<thrift_service::VertexRecord> batch;
    batch.reserve(static_cast<size_t>(batch_size));
    int64_t file_written = 0, file_dup = 0;
    for (const auto& row : csv.rows) {
        thrift_service::VertexRecord rec;

        std::string primary_label = fs.element;
        std::string extra_label;
        if (row_label_col >= 0) {
            const auto& v = row.fields[static_cast<size_t>(row_label_col)];
            extra_label = applyCase(trim(v), fs.row_label->case_mode);
            if (extra_label.empty())
                extra_label.clear();
        }

        auto& labels = *rec.labels();
        labels.push_back(primary_label);
        if (!extra_label.empty() && extra_label != primary_label)
            labels.push_back(extra_label);
        for (const auto& d : extra_labels) {
            if (std::find(labels.begin(), labels.end(), d) == labels.end())
                labels.push_back(d);
        }

        auto& out_props = *rec.properties();
        out_props.resize(merged.size());
        for (size_t mi = 0; mi < merged.size(); ++mi) {
            int col = prop_col_for_merged[mi];
            if (col < 0 || col >= static_cast<int>(row.fields.size()))
                continue;
            PropertySpec spec = merged[mi];
            spec.column = col;
            out_props[mi] = toThriftValue(fs.declared_file, row.line_number, spec, row.fields[static_cast<size_t>(col)],
                                          config.date_format);
        }

        for (const auto& p : pk) {
            thrift_service::PkKey key;
            key.name() = p.name;
            key.value() = toThriftValue(fs.declared_file, row.line_number, p, row.fields[static_cast<size_t>(p.column)],
                                        config.date_format);
            rec.pk()->push_back(std::move(key));
        }

        batch.push_back(std::move(rec));
        if (static_cast<int>(batch.size()) >= batch_size) {
            auto resp = client.batchInsertVertices(primary_label, std::move(batch), "default");
            file_written += *resp.inserted();
            file_dup += *resp.duplicate_pk();
            batch.clear();
            batch.reserve(static_cast<size_t>(batch_size));
        }
    }
    if (!batch.empty()) {
        auto resp = client.batchInsertVertices(fs.element, std::move(batch), "default");
        file_written += *resp.inserted();
        file_dup += *resp.duplicate_pk();
    }

    spdlog::info("[loader] Loaded {} vertices for '{}' ({} duplicate pk skipped)", file_written, where, file_dup);
    written += file_written;
    duplicates += file_dup;
}

void loadOneEdgeFile(shell::EuGraphRpcClient& client, const FileSpec& fs, const SchemaConfig& config,
                     const CsvDialect& dialect, int batch_size, int64_t& written, int64_t& skipped) {
    CsvFile csv = readCsvFile(fs.path, dialect);
    const std::string where = fs.declared_file;

    std::vector<PropertySpec> src_cols = fs.src;
    std::vector<PropertySpec> dst_cols = fs.dst;
    for (auto& c : src_cols)
        c.column = resolveColumn(c, csv.header, where + " (src)", /*required=*/true);
    for (auto& c : dst_cols)
        c.column = resolveColumn(c, csv.header, where + " (dst)", /*required=*/true);

    // 端点主键值来自本文件的列，但发给服务端时要带上**目标标签**的主键属性名与类型
    // （类型必须取目标标签的声明，否则 INT64 主键会被当成 STRING 编码，索引查不到）。
    auto pk_specs_for = [&](const std::string& label) {
        for (const auto& f : config.vertex_files) {
            if (f.element != label || !f.has_pk)
                continue;
            std::vector<PropertySpec> specs;
            for (const auto& p : f.pk) {
                const PropertySpec* declared = nullptr;
                for (const auto& c : f.columns) {
                    if (c.name == p.name) {
                        declared = &c;
                        break;
                    }
                }
                PropertySpec spec = declared != nullptr ? *declared : p;
                // 端点主键值总是从 CSV 原值解析：类型必须已声明（pk 引用的属性在 columns 里必有类型）
                if (!spec.type_declared)
                    throw std::runtime_error("schema: label '" + label + "' primary key property '" + spec.name +
                                             "' must declare a type in columns");
                specs.push_back(spec);
            }
            return specs;
        }
        return std::vector<PropertySpec>{};
    };
    auto src_pk_specs = pk_specs_for(fs.src_label);
    auto dst_pk_specs = pk_specs_for(fs.dst_label);

    // 边属性：按该类型的合并顺序（同类型的多个文件属性按名合并）
    std::vector<PropertySpec> merged;
    for (const auto& f : config.edge_files) {
        if (f.element != fs.element)
            continue;
        for (const auto& c : f.columns) {
            if (std::find_if(merged.begin(), merged.end(), [&](const PropertySpec& p) { return p.name == c.name; }) ==
                merged.end())
                merged.push_back(c);
        }
    }
    std::unordered_map<std::string, int> file_prop_col;
    for (const auto& c : fs.columns) {
        PropertySpec tmp = c;
        file_prop_col[c.name] = resolveColumn(tmp, csv.header, where + " (properties)", /*required=*/true);
    }

    std::vector<thrift_service::EdgeRecord> batch;
    batch.reserve(static_cast<size_t>(batch_size));
    for (const auto& row : csv.rows) {
        thrift_service::EdgeRecord rec;

        auto fill_endpoint = [&](thrift_service::PkRef& ref, const std::string& label,
                                 const std::vector<PropertySpec>& pk_specs,
                                 const std::vector<PropertySpec>& col_specs) {
            ref.primary_label() = label;
            // 端点的每列对应目标标签的一个主键属性（单列主键即 1:1；复合主键必须逐列列出）
            for (size_t k = 0; k < pk_specs.size(); ++k) {
                const PropertySpec& col_spec = col_specs[std::min(k, col_specs.size() - 1)];
                thrift_service::PkKey key;
                key.name() = pk_specs[k].name;
                PropertySpec value_spec = pk_specs[k];
                value_spec.column = col_spec.column;
                key.value() = toThriftValue(where, row.line_number, value_spec,
                                            row.fields[static_cast<size_t>(col_spec.column)], config.date_format);
                ref.keys()->push_back(std::move(key));
            }
        };
        fill_endpoint(*rec.src(), fs.src_label, src_pk_specs, src_cols);
        fill_endpoint(*rec.dst(), fs.dst_label, dst_pk_specs, dst_cols);

        auto& out_props = *rec.properties();
        out_props.resize(merged.size());
        for (size_t mi = 0; mi < merged.size(); ++mi) {
            auto it = file_prop_col.find(merged[mi].name);
            if (it == file_prop_col.end())
                continue;
            PropertySpec spec = merged[mi];
            spec.column = it->second;
            out_props[mi] = toThriftValue(where, row.line_number, spec, row.fields[static_cast<size_t>(it->second)],
                                          config.date_format);
        }

        batch.push_back(std::move(rec));
        if (static_cast<int>(batch.size()) >= batch_size) {
            auto resp = client.batchInsertEdges(fs.element, std::move(batch), "default");
            written += *resp.inserted();
            skipped += *resp.skipped_unresolved();
            batch.clear();
            batch.reserve(static_cast<size_t>(batch_size));
        }
    }
    if (!batch.empty()) {
        auto resp = client.batchInsertEdges(fs.element, std::move(batch), "default");
        written += *resp.inserted();
        skipped += *resp.skipped_unresolved();
    }
    spdlog::info("[loader] Loaded edges for '{}' ({} skipped: endpoint unresolved)", where, written, skipped);
}

} // namespace

std::pair<int64_t, int64_t> loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                                         const SchemaConfig& config, const CsvDialect& dialect, int batch_size,
                                         int concurrency) {
    // 行级标签必须在并发装载之前建好（并发阶段边建边写会有竞态）
    if (!clients.empty()) {
        for (const auto& fs : config.vertex_files)
            createRowLabels(*clients.front(), fs, config, dialect);
    }

    std::atomic<int64_t> written{0}, duplicates{0};
    runFilesParallel(clients, config.vertex_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t i) {
        int64_t w = 0, d = 0;
        loadOneVertexFile(client, config.vertex_files[i], config, dialect, batch_size, w, d);
        written += w;
        duplicates += d;
    });
    return {written.load(), duplicates.load()};
}

std::pair<int64_t, int64_t> loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const SchemaConfig& config,
                                      const CsvDialect& dialect, int batch_size, int concurrency) {
    std::atomic<int64_t> written{0}, skipped{0};
    runFilesParallel(clients, config.edge_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t i) {
        int64_t w = 0, s = 0;
        loadOneEdgeFile(client, config.edge_files[i], config, dialect, batch_size, w, s);
        written += w;
        skipped += s;
    });
    return {written.load(), skipped.load()};
}

std::vector<std::string> findUndeclaredCsvFiles(const SchemaConfig& config) {
    std::set<std::string> declared;
    for (const auto& f : config.vertex_files)
        declared.insert(std::filesystem::weakly_canonical(f.path).string());
    for (const auto& f : config.edge_files)
        declared.insert(std::filesystem::weakly_canonical(f.path).string());

    for (const auto& rel : config.ignored_files) {
        std::filesystem::path p(rel);
        declared.insert(std::filesystem::weakly_canonical(p.is_absolute() ? p : config.data_dir / p).string());
    }

    std::vector<std::string> undeclared;
    if (!std::filesystem::exists(config.data_dir))
        return undeclared;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(config.data_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv")
            continue;
        if (!declared.count(std::filesystem::weakly_canonical(entry.path()).string()))
            undeclared.push_back(std::filesystem::relative(entry.path(), config.data_dir).string());
    }
    std::sort(undeclared.begin(), undeclared.end());
    return undeclared;
}

} // namespace loader
} // namespace eugraph
