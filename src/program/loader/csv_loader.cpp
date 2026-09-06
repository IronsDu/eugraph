#include "program/loader/csv_loader.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace eugraph {
namespace loader {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        e--;
    return s.substr(b, e - b);
}

enum class HeaderKind {
    ID,
    LABEL,
    START_ID,
    END_ID,
    PROPERTY,
};

struct HeaderToken {
    HeaderKind kind = HeaderKind::PROPERTY;
    std::string name;
    std::string group;
    CsvColumnType type = CsvColumnType::STRING;
};

CsvColumnType parseTypeName(const std::string& raw) {
    std::string t = trim(raw);
    if (t == "INT" || t == "LONG")
        return CsvColumnType::INT64;
    if (t == "DOUBLE")
        return CsvColumnType::DOUBLE;
    if (t == "STRING")
        return CsvColumnType::STRING;
    if (t == "BOOL")
        return CsvColumnType::BOOL;
    if (t == "STRING[]")
        return CsvColumnType::STRING_ARRAY;
    if (t == "INT[]" || t == "LONG[]")
        return CsvColumnType::INT64_ARRAY;
    if (t == "DOUBLE[]")
        return CsvColumnType::DOUBLE_ARRAY;
    throw std::runtime_error("Unsupported column type: " + t);
}

std::string extractParen(const std::string& s) {
    auto a = s.find('(');
    auto b = s.rfind(')');
    if (a == std::string::npos || b == std::string::npos || b < a)
        return "";
    return trim(s.substr(a + 1, b - a - 1));
}

HeaderToken parseHeaderToken(const std::string& raw) {
    std::string token = trim(raw);
    HeaderToken ht;

    if (!token.empty() && token[0] == ':') {
        std::string special = token;
        std::string group;
        if (special.rfind(":LABEL", 0) == 0) {
            ht.kind = HeaderKind::LABEL;
            return ht;
        }
        if (special.rfind(":START_ID", 0) == 0) {
            ht.kind = HeaderKind::START_ID;
            ht.group = extractParen(special);
            return ht;
        }
        if (special.rfind(":END_ID", 0) == 0) {
            ht.kind = HeaderKind::END_ID;
            ht.group = extractParen(special);
            return ht;
        }
        if (special.rfind(":ID", 0) == 0) {
            ht.kind = HeaderKind::ID;
            ht.name = "id";
            ht.type = CsvColumnType::INT64;
            ht.group = extractParen(special);
            return ht;
        }
        throw std::runtime_error("Unknown special header column: " + token);
    }

    auto colon = token.find(':');
    if (colon == std::string::npos) {
        ht.kind = HeaderKind::PROPERTY;
        ht.name = token;
        return ht;
    }

    ht.name = trim(token.substr(0, colon));
    std::string spec = trim(token.substr(colon + 1));
    if (spec.rfind("ID", 0) == 0) {
        ht.kind = HeaderKind::ID;
        ht.type = CsvColumnType::INT64;
        ht.group = extractParen(spec);
        return ht;
    }

    ht.kind = HeaderKind::PROPERTY;
    ht.type = parseTypeName(spec);
    return ht;
}

std::string stripCsvSuffix(const std::string& stem) {
    const std::string suffix = "_0_0";
    if (stem.size() > suffix.size() && stem.substr(stem.size() - suffix.size()) == suffix)
        return stem.substr(0, stem.size() - suffix.size());
    return stem;
}

std::vector<std::string> splitArray(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ';')) {
        out.push_back(item);
    }
    return out;
}

} // namespace

std::vector<CsvFileInfo> scanCsvFiles(const std::string& data_dir) {
    std::vector<CsvFileInfo> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv")
            continue;

        std::string stem = entry.path().stem().string();
        std::string core = stripCsvSuffix(stem);
        if (core == stem) {
            continue; // does not end with _0_0
        }

        CsvFileInfo info;
        info.path = entry.path();

        auto first = core.find('_');
        if (first == std::string::npos) {
            // Vertex file: label part may contain '+' for multi-label.
            info.is_vertex = true;
            std::string labels = core;
            std::stringstream ss(labels);
            std::string label;
            while (std::getline(ss, label, '+')) {
                if (!label.empty())
                    info.labels.push_back(label);
            }
            if (info.labels.empty())
                info.labels.push_back(core);
            info.label = info.labels.front();
        } else {
            auto last = core.rfind('_');
            if (last == first) {
                spdlog::warn("[loader] Skipping file with ambiguous edge name: {}", stem);
                continue;
            }
            info.is_vertex = false;
            info.src_label = core.substr(0, first);
            info.edge_type = core.substr(first + 1, last - first - 1);
            info.dst_label = core.substr(last + 1);
        }

        files.push_back(std::move(info));
    }
    return files;
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream iss(line);
    std::string field;
    while (std::getline(iss, field, '|')) {
        fields.push_back(field);
    }
    return fields;
}

PropertyInfo parseTypedColumn(const std::string& token) {
    HeaderToken ht = parseHeaderToken(token);
    if (ht.kind == HeaderKind::LABEL || ht.kind == HeaderKind::START_ID || ht.kind == HeaderKind::END_ID) {
        return {ht.name, ht.type};
    }
    return {ht.name, ht.type};
}

thrift_service::PropertyValueThrift toThriftValue(const std::string& value, CsvColumnType type) {
    thrift_service::PropertyValueThrift tv;
    if (value.empty()) {
        return tv;
    }
    switch (type) {
    case CsvColumnType::INT64:
        tv.set_int_val(std::stoll(value));
        break;
    case CsvColumnType::DOUBLE:
        tv.set_double_val(std::stod(value));
        break;
    case CsvColumnType::STRING:
        tv.set_string_val(value);
        break;
    case CsvColumnType::BOOL:
        tv.set_bool_val(value == "true");
        break;
    case CsvColumnType::STRING_ARRAY:
        tv.set_string_array(splitArray(value));
        break;
    case CsvColumnType::INT64_ARRAY: {
        std::vector<int64_t> arr;
        for (const auto& s : splitArray(value))
            arr.push_back(std::stoll(s));
        tv.set_int_array(std::move(arr));
        break;
    }
    case CsvColumnType::DOUBLE_ARRAY: {
        std::vector<double> arr;
        for (const auto& s : splitArray(value))
            arr.push_back(std::stod(s));
        tv.set_double_array(std::move(arr));
        break;
    }
    }
    return tv;
}

std::vector<LabelSchema> buildLabelSchemas(const std::vector<CsvFileInfo>& vertex_files) {
    std::vector<LabelSchema> result;

    for (const auto& fi : vertex_files) {
        std::string primary = fi.label;

        std::ifstream ifs(fi.path);
        std::string header_line;
        if (!std::getline(ifs, header_line))
            continue;

        auto headers = parseCsvLine(header_line);
        LabelSchema schema;
        schema.file_key = fi.path.string();
        schema.name = primary;
        schema.labels = fi.labels.empty() ? std::vector<std::string>{primary} : fi.labels;
        schema.group = fi.path.stem().string();
        schema.id_col = 0;
        schema.label_col = -1;

        std::vector<std::string> samples;
        std::string line;
        int row = 0;
        while (std::getline(ifs, line) && row < 100) {
            samples.push_back(line);
            row++;
        }

        int num_cols = static_cast<int>(headers.size());
        std::vector<std::vector<std::string>> col_samples(num_cols);
        for (const auto& sample_line : samples) {
            auto fields = parseCsvLine(sample_line);
            for (int i = 0; i < num_cols && i < static_cast<int>(fields.size()); i++) {
                col_samples[i].push_back(fields[i]);
            }
        }

        for (int i = 0; i < num_cols; i++) {
            HeaderToken ht = parseHeaderToken(headers[i]);
            if (ht.kind == HeaderKind::LABEL) {
                schema.label_col = i;
                continue;
            }
            if (ht.kind == HeaderKind::ID) {
                schema.id_col = i;
                PropertyInfo pi;
                pi.name = ht.name;
                pi.type = CsvColumnType::INT64;
                schema.properties.push_back(std::move(pi));
                continue;
            }

            PropertyInfo pi;
            pi.name = ht.name;
            if (ht.type == CsvColumnType::STRING && !headers[i].empty() && headers[i].find(':') == std::string::npos) {
                // Untyped column: fall back to sampling inference.
                bool all_int = true;
                for (const auto& s : col_samples[i]) {
                    if (s.empty())
                        continue;
                    try {
                        size_t pos = 0;
                        std::stoll(s, &pos);
                        if (pos != s.size()) {
                            all_int = false;
                            break;
                        }
                    } catch (...) {
                        all_int = false;
                        break;
                    }
                }
                pi.type = all_int ? CsvColumnType::INT64 : CsvColumnType::STRING;
            } else {
                pi.type = ht.type;
            }
            schema.properties.push_back(std::move(pi));
        }

        result.push_back(std::move(schema));
    }

    return result;
}

std::vector<EdgeTypeSchema> buildEdgeTypeSchemas(const std::vector<CsvFileInfo>& edge_files) {
    std::unordered_map<std::string, EdgeTypeSchema> schema_map;

    for (const auto& fi : edge_files) {
        std::ifstream ifs(fi.path);
        std::string header_line;
        if (!std::getline(ifs, header_line))
            continue;

        auto headers = parseCsvLine(header_line);
        std::vector<std::string> samples;
        std::string line;
        int row = 0;
        while (std::getline(ifs, line) && row < 100) {
            samples.push_back(line);
            row++;
        }

        int num_cols = static_cast<int>(headers.size());
        std::vector<std::vector<std::string>> col_samples(num_cols);
        for (const auto& sample_line : samples) {
            auto fields = parseCsvLine(sample_line);
            for (int i = 0; i < num_cols && i < static_cast<int>(fields.size()); i++) {
                col_samples[i].push_back(fields[i]);
            }
        }

        auto& schema = schema_map[fi.edge_type];
        if (schema.name.empty()) {
            schema.name = fi.edge_type;
            schema.src_label = fi.src_label;
            schema.dst_label = fi.dst_label;
            schema.src_col = 0;
            schema.dst_col = 1;
        }

        for (int i = 0; i < num_cols; i++) {
            HeaderToken ht = parseHeaderToken(headers[i]);
            if (ht.kind == HeaderKind::START_ID) {
                schema.src_col = i;
                if (!ht.group.empty())
                    schema.src_label = ht.group;
                continue;
            }
            if (ht.kind == HeaderKind::END_ID) {
                schema.dst_col = i;
                if (!ht.group.empty())
                    schema.dst_label = ht.group;
                continue;
            }

            PropertyInfo pi;
            pi.name = ht.name;
            if (ht.type == CsvColumnType::STRING && !headers[i].empty() && headers[i].find(':') == std::string::npos) {
                bool all_int = true;
                for (const auto& s : col_samples[i]) {
                    if (s.empty())
                        continue;
                    try {
                        size_t pos = 0;
                        std::stoll(s, &pos);
                        if (pos != s.size()) {
                            all_int = false;
                            break;
                        }
                    } catch (...) {
                        all_int = false;
                        break;
                    }
                }
                pi.type = all_int ? CsvColumnType::INT64 : CsvColumnType::STRING;
            } else {
                pi.type = ht.type;
            }
            schema.properties.push_back(std::move(pi));
        }
    }

    std::vector<EdgeTypeSchema> result;
    for (auto& [_, schema] : schema_map) {
        result.push_back(std::move(schema));
    }
    return result;
}

std::unordered_map<std::string, std::vector<PropertyInfo>>
buildMergedLabelProperties(const std::vector<LabelSchema>& schemas) {
    std::unordered_map<std::string, std::vector<PropertyInfo>> merged;
    for (const auto& schema : schemas) {
        for (const auto& label : schema.labels) {
            auto& props = merged[label];
            for (const auto& pi : schema.properties) {
                bool found = false;
                for (const auto& existing : props) {
                    if (existing.name == pi.name) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    props.push_back(pi);
            }
        }
    }
    return merged;
}

void createLabels(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas) {
    auto merged = buildMergedLabelProperties(schemas);

    for (const auto& [label, props] : merged) {
        std::vector<thrift_service::PropertyDefThrift> thrift_props;
        for (const auto& pi : props) {
            thrift_service::PropertyDefThrift pd;
            pd.name() = pi.name;
            switch (pi.type) {
            case CsvColumnType::INT64:
                pd.type() = thrift_service::PropertyType::INT64;
                break;
            case CsvColumnType::DOUBLE:
                pd.type() = thrift_service::PropertyType::DOUBLE;
                break;
            case CsvColumnType::STRING:
                pd.type() = thrift_service::PropertyType::STRING;
                break;
            case CsvColumnType::BOOL:
                pd.type() = thrift_service::PropertyType::BOOL;
                break;
            case CsvColumnType::STRING_ARRAY:
                pd.type() = thrift_service::PropertyType::STRING_ARRAY;
                break;
            case CsvColumnType::INT64_ARRAY:
                pd.type() = thrift_service::PropertyType::INT64_ARRAY;
                break;
            case CsvColumnType::DOUBLE_ARRAY:
                pd.type() = thrift_service::PropertyType::DOUBLE_ARRAY;
                break;
            }
            pd.is_required() = false;
            thrift_props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating label '{}' with {} properties", label, thrift_props.size());
        client.createLabel(label, thrift_props, "default");
    }
}

void createEdgeLabels(shell::EuGraphRpcClient& client, const std::vector<EdgeTypeSchema>& schemas) {
    for (const auto& schema : schemas) {
        std::vector<thrift_service::PropertyDefThrift> props;
        for (const auto& pi : schema.properties) {
            thrift_service::PropertyDefThrift pd;
            pd.name() = pi.name;
            switch (pi.type) {
            case CsvColumnType::INT64:
                pd.type() = thrift_service::PropertyType::INT64;
                break;
            case CsvColumnType::DOUBLE:
                pd.type() = thrift_service::PropertyType::DOUBLE;
                break;
            case CsvColumnType::STRING:
                pd.type() = thrift_service::PropertyType::STRING;
                break;
            case CsvColumnType::BOOL:
                pd.type() = thrift_service::PropertyType::BOOL;
                break;
            case CsvColumnType::STRING_ARRAY:
                pd.type() = thrift_service::PropertyType::STRING_ARRAY;
                break;
            case CsvColumnType::INT64_ARRAY:
                pd.type() = thrift_service::PropertyType::INT64_ARRAY;
                break;
            case CsvColumnType::DOUBLE_ARRAY:
                pd.type() = thrift_service::PropertyType::DOUBLE_ARRAY;
                break;
            }
            pd.is_required() = false;
            props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating edge label '{}' with {} properties", schema.name, props.size());
        client.createEdgeLabel(schema.name, props, "default");
    }
}

template <typename FileFn>
void runFilesParallel(const std::vector<shell::EuGraphRpcClient*>& clients, size_t file_count, int concurrency,
                      FileFn&& fn);

// ==================== Vertex loading ====================

using VertexSchemaMap = std::unordered_map<std::string, const LabelSchema*>;
using EdgeSchemaMap = std::unordered_map<std::string, const EdgeTypeSchema*>;

void loadOneVertexFile(shell::EuGraphRpcClient& client, const CsvFileInfo& fi, const VertexSchemaMap& schema_map,
                       const std::unordered_map<std::string, std::vector<PropertyInfo>>& merged_label_props,
                       int batch_size, CsvIdMap& group_map,
                       std::unordered_map<std::string, std::string>& label_to_group) {
    auto it = schema_map.find(fi.path.string());
    if (it == schema_map.end()) {
        spdlog::warn("[loader] No schema for file '{}', skipping", fi.path.string());
        return;
    }
    const auto& schema = *it->second;

    // Pre-create row-level labels from the :LABEL column so multi-label batch
    // insertion never hits "Label not found" during the data pass.
    if (schema.label_col >= 0) {
        std::vector<thrift_service::PropertyDefThrift> label_props;
        for (const auto& pi : schema.properties) {
            thrift_service::PropertyDefThrift pd;
            pd.name() = pi.name;
            switch (pi.type) {
            case CsvColumnType::INT64:
                pd.type() = thrift_service::PropertyType::INT64;
                break;
            case CsvColumnType::DOUBLE:
                pd.type() = thrift_service::PropertyType::DOUBLE;
                break;
            case CsvColumnType::STRING:
                pd.type() = thrift_service::PropertyType::STRING;
                break;
            case CsvColumnType::BOOL:
                pd.type() = thrift_service::PropertyType::BOOL;
                break;
            case CsvColumnType::STRING_ARRAY:
                pd.type() = thrift_service::PropertyType::STRING_ARRAY;
                break;
            case CsvColumnType::INT64_ARRAY:
                pd.type() = thrift_service::PropertyType::INT64_ARRAY;
                break;
            case CsvColumnType::DOUBLE_ARRAY:
                pd.type() = thrift_service::PropertyType::DOUBLE_ARRAY;
                break;
            }
            pd.is_required() = false;
            label_props.push_back(std::move(pd));
        }

        std::ifstream pre(fi.path);
        std::string pre_header;
        if (std::getline(pre, pre_header)) {
            std::unordered_set<std::string> row_labels;
            std::string pre_line;
            while (std::getline(pre, pre_line)) {
                auto fields = parseCsvLine(pre_line);
                if (schema.label_col < static_cast<int>(fields.size())) {
                    std::string row_label = trim(fields[schema.label_col]);
                    if (!row_label.empty())
                        row_labels.insert(row_label);
                }
            }
            for (const auto& row_label : row_labels)
                client.createLabel(row_label, label_props, "default");
        }
    }

    std::ifstream ifs(fi.path);
    std::string header_line;
    if (!std::getline(ifs, header_line))
        return;
    auto headers = parseCsvLine(header_line);

    // Property columns = every column except :LABEL. The ID column is
    // included and stored as a regular property.
    std::vector<int> prop_cols;
    for (int i = 0; i < static_cast<int>(headers.size()); i++) {
        HeaderToken ht = parseHeaderToken(headers[i]);
        if (ht.kind == HeaderKind::LABEL)
            continue;
        prop_cols.push_back(i);
    }

    struct BatchState {
        std::vector<int64_t> csv_ids;
        std::vector<thrift_service::VertexRecord> records;
    };
    std::unordered_map<std::string, BatchState> batches;
    int total = 0;

    auto& file_label_map = group_map[schema.group];
    for (const auto& label : schema.labels)
        label_to_group[label] = schema.group;

    auto flush_batch = [&](const std::string& primary_label) {
        auto batch_it = batches.find(primary_label);
        if (batch_it == batches.end() || batch_it->second.records.empty())
            return;
        auto& batch = batch_it->second;
        auto result = client.batchInsertVertices(primary_label, std::move(batch.records), "default");
        for (size_t i = 0; i < result.vertex_ids()->size() && i < batch.csv_ids.size(); i++) {
            file_label_map[batch.csv_ids[i]] = static_cast<uint64_t>((*result.vertex_ids())[i]);
        }
        batch.csv_ids.clear();
        batch.records.clear();
        batch.csv_ids.reserve(batch_size);
        batch.records.reserve(batch_size);
    };

    std::string line;
    while (std::getline(ifs, line)) {
        auto fields = parseCsvLine(line);
        if (fields.empty())
            continue;

        int64_t csv_id = std::stoll(fields[schema.id_col]);

        std::string row_label;
        if (schema.label_col >= 0 && schema.label_col < static_cast<int>(fields.size()))
            row_label = trim(fields[schema.label_col]);

        // Row-level labels take precedence as primary label so properties
        // land under the label used by query index access (City/Country/...).
        std::string primary_label = !row_label.empty() ? row_label : schema.name;

        thrift_service::VertexRecord rec;
        auto& props = *rec.properties();
        auto& labels = *rec.labels();

        labels.push_back(primary_label);
        for (const auto& label : schema.labels) {
            if (label != primary_label)
                labels.push_back(label);
        }
        if (!row_label.empty()) {
            if (std::find(labels.begin(), labels.end(), row_label) == labels.end())
                labels.push_back(row_label);
            label_to_group[row_label] = schema.group;
        }

        // Map file-schema properties to the primary label's property order
        // (properties are stored under the primary label with its own prop IDs).
        auto order_it = merged_label_props.find(primary_label);
        std::vector<thrift_service::PropertyValueThrift> ordered_props;
        if (order_it != merged_label_props.end()) {
            ordered_props.resize(order_it->second.size());
            for (size_t i = 0; i < schema.properties.size(); i++) {
                const auto& pi = schema.properties[i];
                int csv_col = prop_cols[i];
                for (size_t j = 0; j < order_it->second.size(); j++) {
                    if (order_it->second[j].name == pi.name) {
                        if (csv_col < static_cast<int>(fields.size())) {
                            ordered_props[j] = toThriftValue(fields[csv_col], pi.type);
                        } else {
                            ordered_props[j] = thrift_service::PropertyValueThrift{};
                        }
                        break;
                    }
                }
            }
            props = std::move(ordered_props);
        } else {
            for (size_t i = 0; i < schema.properties.size(); i++) {
                int csv_col = prop_cols[i];
                if (csv_col < static_cast<int>(fields.size())) {
                    props.push_back(toThriftValue(fields[csv_col], schema.properties[i].type));
                } else {
                    props.push_back(thrift_service::PropertyValueThrift{});
                }
            }
        }

        auto& batch = batches[primary_label];
        batch.csv_ids.push_back(csv_id);
        batch.records.push_back(std::move(rec));
        total++;

        if (static_cast<int>(batch.records.size()) >= batch_size)
            flush_batch(primary_label);
    }

    for (auto& [primary_label, batch] : batches)
        flush_batch(primary_label);

    spdlog::info("[loader] Loaded {} vertices for file '{}'", total, fi.path.string());
}

std::vector<int> buildVertexPropertyColumns(const std::vector<std::string>& headers, const LabelSchema& schema) {
    std::vector<int> cols;
    for (int i = 0; i < static_cast<int>(headers.size()); i++) {
        HeaderToken ht = parseHeaderToken(headers[i]);
        if (ht.kind == HeaderKind::LABEL)
            continue;
        cols.push_back(i);
    }
    // Ensure size matches properties (ID column included, label excluded).
    return cols;
}

std::vector<int> buildEdgePropertyColumns(const std::vector<std::string>& headers, const EdgeTypeSchema& schema) {
    std::vector<int> cols;
    for (int i = 0; i < static_cast<int>(headers.size()); i++) {
        HeaderToken ht = parseHeaderToken(headers[i]);
        if (ht.kind == HeaderKind::START_ID || ht.kind == HeaderKind::END_ID)
            continue;
        cols.push_back(i);
    }
    return cols;
}

LoadedIdMaps loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                          const std::vector<CsvFileInfo>& vertex_files, const std::vector<LabelSchema>& label_schemas,
                          const std::unordered_map<std::string, std::vector<PropertyInfo>>& merged_label_props,
                          int batch_size, int concurrency) {
    VertexSchemaMap schema_map;
    for (const auto& s : label_schemas)
        schema_map[s.file_key] = &s;

    LoadedIdMaps maps;
    std::mutex maps_mu;

    runFilesParallel(clients, vertex_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t index) {
        CsvIdMap group_map;
        std::unordered_map<std::string, std::string> label_to_group;
        loadOneVertexFile(client, vertex_files[index], schema_map, merged_label_props, batch_size, group_map,
                          label_to_group);
        std::lock_guard<std::mutex> lock(maps_mu);
        for (auto& [group, m] : group_map) {
            auto& dest = maps.group_id_map[group];
            for (auto& [csv_id, vid] : m)
                dest[csv_id] = vid;
        }
        for (auto& [label, group] : label_to_group)
            maps.label_to_group[label] = group;
    });

    return maps;
}

LoadedIdMaps loadVertices(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& vertex_files,
                          const std::vector<LabelSchema>& label_schemas,
                          const std::unordered_map<std::string, std::vector<PropertyInfo>>& merged_label_props,
                          int batch_size) {
    std::vector<shell::EuGraphRpcClient*> clients{&client};
    return loadVertices(clients, vertex_files, label_schemas, merged_label_props, batch_size, 1);
}

// ==================== Unique indexes ====================

void createUniqueIdIndexes(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas) {
    std::unordered_set<std::string> seen_labels;
    std::unordered_map<std::string, std::string> id_prop_names;
    for (const auto& schema : schemas) {
        for (const auto& label : schema.labels) {
            if (!schema.properties.empty() && id_prop_names.find(label) == id_prop_names.end())
                id_prop_names[label] = schema.properties[0].name;
        }
    }

    for (const auto& schema : schemas) {
        for (const auto& label : schema.labels) {
            if (!seen_labels.insert(label).second)
                continue;
            if (schema.properties.empty())
                continue;

            const auto& id_prop_name = id_prop_names[label];
            std::string index_name = fmt::format("idx_{}_{}_unique", label, id_prop_name);
            std::string query =
                fmt::format("CREATE UNIQUE INDEX {} FOR (n:{}) ON (n.{})", index_name, label, id_prop_name);
            try {
                spdlog::info("[loader] Creating unique index '{}' on label '{}' property '{}'", index_name, label,
                             id_prop_name);
                client.executeCypher(query, "default");
                spdlog::info("[loader] Unique index '{}' created successfully", index_name);
            } catch (const std::exception& e) {
                spdlog::warn("[loader] Failed to create unique index '{}': {}", index_name, e.what());
            }
        }
    }
}

// ==================== Edge loading ====================
// ==================== Edge loading ====================

void loadOneEdgeFile(shell::EuGraphRpcClient& client, const CsvFileInfo& fi, const EdgeSchemaMap& schema_map,
                     const LoadedIdMaps& id_maps, int batch_size) {
    auto it = schema_map.find(fi.edge_type);
    if (it == schema_map.end()) {
        spdlog::warn("[loader] No schema for edge type '{}', skipping {}", fi.edge_type, fi.path.string());
        return;
    }
    const auto& schema = *it->second;

    std::ifstream ifs(fi.path);
    std::string header_line;
    if (!std::getline(ifs, header_line))
        return;
    auto headers = parseCsvLine(header_line);

    // Per-file src/dst columns and group labels come from the header
    // (:START_ID(Group) / :END_ID(Group)); schema provides defaults.
    int src_col = schema.src_col;
    int dst_col = schema.dst_col;
    std::string src_label = schema.src_label;
    std::string dst_label = schema.dst_label;
    for (int i = 0; i < static_cast<int>(headers.size()); i++) {
        HeaderToken ht = parseHeaderToken(headers[i]);
        if (ht.kind == HeaderKind::START_ID) {
            src_col = i;
            if (!ht.group.empty())
                src_label = ht.group;
        } else if (ht.kind == HeaderKind::END_ID) {
            dst_col = i;
            if (!ht.group.empty())
                dst_label = ht.group;
        }
    }

    auto src_group_it = id_maps.label_to_group.find(src_label);
    auto dst_group_it = id_maps.label_to_group.find(dst_label);
    if (src_group_it == id_maps.label_to_group.end() || dst_group_it == id_maps.label_to_group.end()) {
        spdlog::warn("[loader] Missing vertex group for {} -> {}, skipping", src_label, dst_label);
        return;
    }
    auto src_map_it = id_maps.group_id_map.find(src_group_it->second);
    auto dst_map_it = id_maps.group_id_map.find(dst_group_it->second);
    if (src_map_it == id_maps.group_id_map.end() || dst_map_it == id_maps.group_id_map.end()) {
        spdlog::warn("[loader] Missing vertex ID map for {} -> {}, skipping", src_label, dst_label);
        return;
    }
    const auto& src_map = src_map_it->second;
    const auto& dst_map = dst_map_it->second;

    std::vector<int> prop_cols = buildEdgePropertyColumns(headers, schema);

    std::vector<thrift_service::EdgeRecord> batch;
    batch.reserve(batch_size);
    int total = 0;
    int skipped = 0;

    std::string line;
    while (std::getline(ifs, line)) {
        auto fields = parseCsvLine(line);
        if (fields.empty())
            continue;

        int64_t src_csv_id = std::stoll(fields[src_col]);
        int64_t dst_csv_id = std::stoll(fields[dst_col]);

        auto src_vid_it = src_map.find(src_csv_id);
        auto dst_vid_it = dst_map.find(dst_csv_id);
        if (src_vid_it == src_map.end() || dst_vid_it == dst_map.end()) {
            skipped++;
            continue;
        }

        thrift_service::EdgeRecord rec;
        rec.src_vertex_id() = static_cast<int64_t>(src_vid_it->second);
        rec.dst_vertex_id() = static_cast<int64_t>(dst_vid_it->second);

        auto& props = *rec.properties();
        for (size_t i = 0; i < schema.properties.size(); i++) {
            int csv_col = prop_cols[i];
            if (csv_col < static_cast<int>(fields.size())) {
                props.push_back(toThriftValue(fields[csv_col], schema.properties[i].type));
            } else {
                props.push_back(thrift_service::PropertyValueThrift{});
            }
        }

        batch.push_back(std::move(rec));
        total++;

        if (static_cast<int>(batch.size()) >= batch_size) {
            client.batchInsertEdges(fi.edge_type, std::move(batch), "default");
            batch.clear();
            batch.reserve(batch_size);
        }
    }

    if (!batch.empty())
        client.batchInsertEdges(fi.edge_type, std::move(batch), "default");

    spdlog::info("[loader] Loaded {} edges for '{}' ({} skipped)", total, fi.edge_type, skipped);
}

template <typename FileFn>
void runFilesParallel(const std::vector<shell::EuGraphRpcClient*>& clients, size_t file_count, int concurrency,
                      FileFn&& fn) {
    if (file_count == 0 || clients.empty())
        return;

    if (concurrency <= 1 || file_count == 1) {
        for (size_t i = 0; i < file_count; ++i) {
            fn(*clients[0], i);
        }
        return;
    }

    int workers = std::min<int>(concurrency, static_cast<int>(file_count));
    std::atomic<size_t> next_index{0};
    std::mutex error_mu;
    std::exception_ptr error;

    auto worker = [&](int worker_id) {
        shell::EuGraphRpcClient& client = *clients[worker_id % clients.size()];
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
    threads.reserve(workers);
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    if (error)
        std::rethrow_exception(error);
}

void loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const LoadedIdMaps& id_maps, int batch_size,
               int concurrency) {
    EdgeSchemaMap schema_map;
    for (const auto& s : edge_schemas)
        schema_map[s.name] = &s;

    runFilesParallel(clients, edge_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t index) {
        loadOneEdgeFile(client, edge_files[index], schema_map, id_maps, batch_size);
    });
}

void loadEdges(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const LoadedIdMaps& id_maps, int batch_size) {
    std::vector<shell::EuGraphRpcClient*> clients{&client};
    loadEdges(clients, edge_files, edge_schemas, id_maps, batch_size, 1);
}

// ==================== CLI spec parsing ====================

bool parseNodeSpec(const std::string& spec, CsvFileInfo& info, std::string& error) {
    auto eq = spec.rfind('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
        error = "invalid --nodes spec (expected Label[:Label...]=file): " + spec;
        return false;
    }
    std::string labels_part = spec.substr(0, eq);
    std::string file_part = spec.substr(eq + 1);
    if (labels_part.empty() || file_part.empty()) {
        error = "invalid --nodes spec: " + spec;
        return false;
    }

    info.is_vertex = true;
    info.path = file_part;
    std::stringstream ss(labels_part);
    std::string label;
    while (std::getline(ss, label, ':')) {
        if (!label.empty())
            info.labels.push_back(label);
    }
    if (info.labels.empty()) {
        error = "invalid --nodes spec (empty label list): " + spec;
        return false;
    }
    info.label = info.labels.front();
    return true;
}

bool parseRelationshipSpec(const std::string& spec, CsvFileInfo& info, std::string& error) {
    auto eq = spec.rfind('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
        error = "invalid --relationships spec (expected TYPE=file): " + spec;
        return false;
    }
    std::string type_part = spec.substr(0, eq);
    std::string file_part = spec.substr(eq + 1);
    if (type_part.empty() || file_part.empty()) {
        error = "invalid --relationships spec: " + spec;
        return false;
    }

    info.is_vertex = false;
    info.path = file_part;
    info.edge_type = type_part;
    // src/dst labels are filled by header parsing (START_ID/END_ID); these
    // defaults are only used when headers are legacy first/second columns.
    info.src_label.clear();
    info.dst_label.clear();
    return true;
}

} // namespace loader
} // namespace eugraph
