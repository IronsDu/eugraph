#include "loader/csv_loader.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

namespace eugraph {
namespace loader {

std::vector<CsvFileInfo> scanCsvFiles(const std::string& data_dir) {
    std::vector<CsvFileInfo> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv")
            continue;

        std::string stem = entry.path().stem().string();
        // Split by '_'
        std::vector<std::string> parts;
        std::istringstream iss(stem);
        std::string part;
        while (std::getline(iss, part, '_')) {
            parts.push_back(part);
        }

        CsvFileInfo info;
        info.path = entry.path();

        // 3 parts: label_0_0 -> vertex file
        // 5 parts: srcLabel_edgeType_dstLabel_0_0 -> edge file
        if (parts.size() == 3) {
            info.is_vertex = true;
            info.label = parts[0];
        } else if (parts.size() == 5) {
            info.is_vertex = false;
            info.src_label = parts[0];
            info.edge_type = parts[1];
            info.dst_label = parts[2];
        } else {
            continue; // skip unrecognized patterns
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

PropertyInfo inferPropertyType(const std::string& name, const std::vector<std::string>& samples) {
    bool all_int = true;
    for (const auto& s : samples) {
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
    return {name, all_int};
}

std::vector<LabelSchema> buildLabelSchemas(const std::vector<CsvFileInfo>& vertex_files) {
    std::unordered_map<std::string, LabelSchema> schema_map;

    for (const auto& fi : vertex_files) {
        if (schema_map.count(fi.label))
            continue;

        std::ifstream ifs(fi.path);
        std::string header_line;
        if (!std::getline(ifs, header_line))
            continue;

        auto headers = parseCsvLine(header_line);
        // First column is 'id', skip it

        // Sample up to 100 rows for type inference
        std::vector<std::string> samples;
        std::string line;
        int row = 0;
        while (std::getline(ifs, line) && row < 100) {
            samples.push_back(line);
            row++;
        }

        LabelSchema schema;
        schema.name = fi.label;

        // Parse sample rows column by column
        int num_cols = headers.size();
        std::vector<std::vector<std::string>> col_samples(num_cols - 1);
        for (const auto& sample_line : samples) {
            auto fields = parseCsvLine(sample_line);
            for (int i = 1; i < num_cols && i < (int)fields.size(); i++) {
                col_samples[i - 1].push_back(fields[i]);
            }
        }

        for (int i = 1; i < num_cols; i++) {
            auto pi = inferPropertyType(headers[i], col_samples[i - 1]);
            schema.properties.push_back(std::move(pi));
        }

        schema_map[fi.label] = std::move(schema);
    }

    std::vector<LabelSchema> result;
    for (auto& [_, schema] : schema_map) {
        result.push_back(std::move(schema));
    }
    return result;
}

std::vector<EdgeTypeSchema> buildEdgeTypeSchemas(const std::vector<CsvFileInfo>& edge_files) {
    std::unordered_map<std::string, EdgeTypeSchema> schema_map;

    for (const auto& fi : edge_files) {
        // Merge properties across files with same edge type
        std::ifstream ifs(fi.path);
        std::string header_line;
        if (!std::getline(ifs, header_line))
            continue;

        auto headers = parseCsvLine(header_line);
        // First two columns are src/dst id references, skip them

        // Sample rows
        std::vector<std::string> samples;
        std::string line;
        int row = 0;
        while (std::getline(ifs, line) && row < 100) {
            samples.push_back(line);
            row++;
        }

        int num_cols = headers.size();
        std::vector<std::vector<std::string>> col_samples(num_cols - 2);
        for (const auto& sample_line : samples) {
            auto fields = parseCsvLine(sample_line);
            for (int i = 2; i < num_cols && i < (int)fields.size(); i++) {
                col_samples[i - 2].push_back(fields[i]);
            }
        }

        auto& schema = schema_map[fi.edge_type];
        if (schema.name.empty()) {
            schema.name = fi.edge_type;
        }

        for (int i = 2; i < num_cols; i++) {
            // Check if property already exists (avoid duplicates when merging)
            bool exists = false;
            for (const auto& p : schema.properties) {
                if (p.name == headers[i]) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                auto pi = inferPropertyType(headers[i], col_samples[i - 2]);
                schema.properties.push_back(std::move(pi));
            }
        }
    }

    std::vector<EdgeTypeSchema> result;
    for (auto& [_, schema] : schema_map) {
        result.push_back(std::move(schema));
    }
    return result;
}

thrift::PropertyValueThrift toThriftValue(const std::string& value, bool is_int64) {
    thrift::PropertyValueThrift tv;
    if (value.empty()) {
        // Leave as __EMPTY__
        return tv;
    }
    if (is_int64) {
        tv.set_int_val(std::stoll(value));
    } else {
        tv.set_string_val(value);
    }
    return tv;
}

void createLabels(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas) {
    for (const auto& schema : schemas) {
        std::vector<thrift::PropertyDefThrift> props;
        for (const auto& pi : schema.properties) {
            thrift::PropertyDefThrift pd;
            pd.name() = pi.name;
            pd.type() = pi.is_int64 ? thrift::PropertyType::INT64 : thrift::PropertyType::STRING;
            pd.is_required() = false;
            props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating label '{}' with {} properties", schema.name, props.size());
        client.createLabel(schema.name, props);
    }
}

void createEdgeLabels(shell::EuGraphRpcClient& client, const std::vector<EdgeTypeSchema>& schemas) {
    for (const auto& schema : schemas) {
        std::vector<thrift::PropertyDefThrift> props;
        for (const auto& pi : schema.properties) {
            thrift::PropertyDefThrift pd;
            pd.name() = pi.name;
            pd.type() = pi.is_int64 ? thrift::PropertyType::INT64 : thrift::PropertyType::STRING;
            pd.is_required() = false;
            props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating edge label '{}' with {} properties", schema.name, props.size());
        client.createEdgeLabel(schema.name, props);
    }
}

CsvIdMap loadVertices(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& vertex_files,
                      const std::vector<LabelSchema>& label_schemas, int batch_size) {
    std::unordered_map<std::string, const LabelSchema*> schema_map;
    for (const auto& s : label_schemas) {
        schema_map[s.name] = &s;
    }

    CsvIdMap id_map;

    for (const auto& fi : vertex_files) {
        auto it = schema_map.find(fi.label);
        if (it == schema_map.end()) {
            spdlog::warn("[loader] No schema for label '{}', skipping {}", fi.label, fi.path.string());
            continue;
        }
        const auto& schema = *it->second;

        std::ifstream ifs(fi.path);
        std::string header_line;
        std::getline(ifs, header_line); // skip header

        std::vector<int64_t> csv_ids;
        std::vector<thrift::VertexRecord> batch;
        csv_ids.reserve(batch_size);
        batch.reserve(batch_size);
        int total = 0;
        auto& label_map = id_map[fi.label];

        auto flush_batch = [&]() {
            if (batch.empty())
                return;
            auto result = client.batchInsertVertices(fi.label, std::move(batch));
            for (size_t i = 0; i < result.vertex_ids()->size() && i < csv_ids.size(); i++) {
                label_map[csv_ids[i]] = static_cast<uint64_t>((*result.vertex_ids())[i]);
            }
            csv_ids.clear();
            batch.clear();
            csv_ids.reserve(batch_size);
            batch.reserve(batch_size);
        };

        std::string line;
        while (std::getline(ifs, line)) {
            auto fields = parseCsvLine(line);
            if (fields.empty())
                continue;

            int64_t csv_id = std::stoll(fields[0]);
            csv_ids.push_back(csv_id);

            thrift::VertexRecord rec;
            rec.pk_value() = toThriftValue(fields[0], true);

            auto& props = *rec.properties();
            for (size_t i = 0; i < schema.properties.size(); i++) {
                if (i + 1 < fields.size()) {
                    props.push_back(toThriftValue(fields[i + 1], schema.properties[i].is_int64));
                } else {
                    props.push_back(thrift::PropertyValueThrift{});
                }
            }

            batch.push_back(std::move(rec));
            total++;

            if ((int)batch.size() >= batch_size) {
                flush_batch();
            }
        }

        flush_batch();
        spdlog::info("[loader] Loaded {} vertices for label '{}'", total, fi.label);
    }

    return id_map;
}

void loadEdges(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const CsvIdMap& id_map, int batch_size) {
    // Build edge type -> schema lookup
    std::unordered_map<std::string, const EdgeTypeSchema*> schema_map;
    for (const auto& s : edge_schemas) {
        schema_map[s.name] = &s;
    }

    for (const auto& fi : edge_files) {
        auto it = schema_map.find(fi.edge_type);
        if (it == schema_map.end()) {
            spdlog::warn("[loader] No schema for edge type '{}', skipping {}", fi.edge_type, fi.path.string());
            continue;
        }
        const auto& schema = *it->second;

        auto src_it = id_map.find(fi.src_label);
        auto dst_it = id_map.find(fi.dst_label);
        if (src_it == id_map.end() || dst_it == id_map.end()) {
            spdlog::warn("[loader] Missing vertex ID map for {} -> {}, skipping", fi.src_label, fi.dst_label);
            continue;
        }
        const auto& src_map = src_it->second;
        const auto& dst_map = dst_it->second;

        std::ifstream ifs(fi.path);
        std::string header_line;
        std::getline(ifs, header_line); // skip header

        std::vector<thrift::EdgeRecord> batch;
        batch.reserve(batch_size);
        int total = 0;
        int skipped = 0;

        std::string line;
        while (std::getline(ifs, line)) {
            auto fields = parseCsvLine(line);
            if (fields.size() < 2)
                continue;

            int64_t src_csv_id = std::stoll(fields[0]);
            int64_t dst_csv_id = std::stoll(fields[1]);

            auto src_vid_it = src_map.find(src_csv_id);
            auto dst_vid_it = dst_map.find(dst_csv_id);
            if (src_vid_it == src_map.end() || dst_vid_it == dst_map.end()) {
                skipped++;
                continue;
            }

            thrift::EdgeRecord rec;
            rec.src_vertex_id() = static_cast<int64_t>(src_vid_it->second);
            rec.dst_vertex_id() = static_cast<int64_t>(dst_vid_it->second);

            auto& props = *rec.properties();
            for (size_t i = 0; i < schema.properties.size(); i++) {
                if (i + 2 < fields.size()) {
                    props.push_back(toThriftValue(fields[i + 2], schema.properties[i].is_int64));
                } else {
                    props.push_back(thrift::PropertyValueThrift{});
                }
            }

            batch.push_back(std::move(rec));
            total++;

            if ((int)batch.size() >= batch_size) {
                client.batchInsertEdges(fi.edge_type, std::move(batch));
                batch.clear();
                batch.reserve(batch_size);
            }
        }

        if (!batch.empty()) {
            client.batchInsertEdges(fi.edge_type, std::move(batch));
        }

        spdlog::info("[loader] Loaded {} edges for '{}' ({} skipped)", total, fi.edge_type, skipped);
    }
}

} // namespace loader
} // namespace eugraph
