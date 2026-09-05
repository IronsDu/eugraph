#include "program/loader/csv_loader.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

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

        // Parse sample rows column by column (all columns are properties)
        int num_cols = headers.size();
        std::vector<std::vector<std::string>> col_samples(num_cols);
        for (const auto& sample_line : samples) {
            auto fields = parseCsvLine(sample_line);
            for (int i = 0; i < num_cols && i < (int)fields.size(); i++) {
                col_samples[i].push_back(fields[i]);
            }
        }

        for (int i = 0; i < num_cols; i++) {
            auto pi = inferPropertyType(headers[i], col_samples[i]);
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

thrift_service::PropertyValueThrift toThriftValue(const std::string& value, bool is_int64) {
    thrift_service::PropertyValueThrift tv;
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
        std::vector<thrift_service::PropertyDefThrift> props;
        for (const auto& pi : schema.properties) {
            thrift_service::PropertyDefThrift pd;
            pd.name() = pi.name;
            pd.type() = pi.is_int64 ? thrift_service::PropertyType::INT64 : thrift_service::PropertyType::STRING;
            pd.is_required() = false;
            props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating label '{}' with {} properties", schema.name, props.size());
        client.createLabel(schema.name, props, "default");
    }
}

void createEdgeLabels(shell::EuGraphRpcClient& client, const std::vector<EdgeTypeSchema>& schemas) {
    for (const auto& schema : schemas) {
        std::vector<thrift_service::PropertyDefThrift> props;
        for (const auto& pi : schema.properties) {
            thrift_service::PropertyDefThrift pd;
            pd.name() = pi.name;
            pd.type() = pi.is_int64 ? thrift_service::PropertyType::INT64 : thrift_service::PropertyType::STRING;
            pd.is_required() = false;
            props.push_back(std::move(pd));
        }
        spdlog::info("[loader] Creating edge label '{}' with {} properties", schema.name, props.size());
        client.createEdgeLabel(schema.name, props, "default");
    }
}

// ==================== Concurrent file runner ====================

namespace {

using VertexSchemaMap = std::unordered_map<std::string, const LabelSchema*>;
using EdgeSchemaMap = std::unordered_map<std::string, const EdgeTypeSchema*>;

CsvIdMap loadOneVertexFile(shell::EuGraphRpcClient& client, const CsvFileInfo& fi, const VertexSchemaMap& schema_map,
                           int batch_size) {
    auto it = schema_map.find(fi.label);
    if (it == schema_map.end()) {
        spdlog::warn("[loader] No schema for label '{}', skipping {}", fi.label, fi.path.string());
        return {};
    }
    const auto& schema = *it->second;

    std::ifstream ifs(fi.path);
    std::string header_line;
    std::getline(ifs, header_line); // skip header

    std::vector<int64_t> csv_ids;
    std::vector<thrift_service::VertexRecord> batch;
    csv_ids.reserve(batch_size);
    batch.reserve(batch_size);
    int total = 0;

    CsvIdMap file_id_map;
    auto& label_map = file_id_map[fi.label];

    auto flush_batch = [&]() {
        if (batch.empty())
            return;
        auto result = client.batchInsertVertices(fi.label, std::move(batch), "default");
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

        thrift_service::VertexRecord rec;
        auto& props = *rec.properties();
        for (size_t i = 0; i < schema.properties.size(); i++) {
            if (i < fields.size()) {
                props.push_back(toThriftValue(fields[i], schema.properties[i].is_int64));
            } else {
                props.push_back(thrift_service::PropertyValueThrift{});
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
    return file_id_map;
}

void loadOneEdgeFile(shell::EuGraphRpcClient& client, const CsvFileInfo& fi, const EdgeSchemaMap& schema_map,
                     const CsvIdMap& id_map, int batch_size) {
    auto it = schema_map.find(fi.edge_type);
    if (it == schema_map.end()) {
        spdlog::warn("[loader] No schema for edge type '{}', skipping {}", fi.edge_type, fi.path.string());
        return;
    }
    const auto& schema = *it->second;

    auto src_it = id_map.find(fi.src_label);
    auto dst_it = id_map.find(fi.dst_label);
    if (src_it == id_map.end() || dst_it == id_map.end()) {
        spdlog::warn("[loader] Missing vertex ID map for {} -> {}, skipping", fi.src_label, fi.dst_label);
        return;
    }
    const auto& src_map = src_it->second;
    const auto& dst_map = dst_it->second;

    std::ifstream ifs(fi.path);
    std::string header_line;
    std::getline(ifs, header_line); // skip header

    std::vector<thrift_service::EdgeRecord> batch;
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

        thrift_service::EdgeRecord rec;
        rec.src_vertex_id() = static_cast<int64_t>(src_vid_it->second);
        rec.dst_vertex_id() = static_cast<int64_t>(dst_vid_it->second);

        auto& props = *rec.properties();
        for (size_t i = 0; i < schema.properties.size(); i++) {
            if (i + 2 < fields.size()) {
                props.push_back(toThriftValue(fields[i + 2], schema.properties[i].is_int64));
            } else {
                props.push_back(thrift_service::PropertyValueThrift{});
            }
        }

        batch.push_back(std::move(rec));
        total++;

        if ((int)batch.size() >= batch_size) {
            client.batchInsertEdges(fi.edge_type, std::move(batch), "default");
            batch.clear();
            batch.reserve(batch_size);
        }
    }

    if (!batch.empty()) {
        client.batchInsertEdges(fi.edge_type, std::move(batch), "default");
    }

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

} // namespace

// ==================== Vertex loading ====================

CsvIdMap loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                      const std::vector<CsvFileInfo>& vertex_files, const std::vector<LabelSchema>& label_schemas,
                      int batch_size, int concurrency) {
    VertexSchemaMap schema_map;
    for (const auto& s : label_schemas) {
        schema_map[s.name] = &s;
    }

    std::vector<CsvIdMap> file_id_maps(vertex_files.size());
    runFilesParallel(clients, vertex_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t index) {
        file_id_maps[index] = loadOneVertexFile(client, vertex_files[index], schema_map, batch_size);
    });

    CsvIdMap id_map;
    for (auto& file_map : file_id_maps) {
        for (auto& [label, label_map] : file_map) {
            auto& dest = id_map[label];
            for (auto& [csv_id, vertex_id] : label_map) {
                dest[csv_id] = vertex_id;
            }
        }
    }
    return id_map;
}

CsvIdMap loadVertices(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& vertex_files,
                      const std::vector<LabelSchema>& label_schemas, int batch_size) {
    std::vector<shell::EuGraphRpcClient*> clients{&client};
    return loadVertices(clients, vertex_files, label_schemas, batch_size, 1);
}

// ==================== Unique indexes ====================

void createUniqueIdIndexes(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas) {
    for (const auto& schema : schemas) {
        if (schema.properties.empty())
            continue;

        const auto& id_prop_name = schema.properties[0].name;
        std::string index_name = fmt::format("idx_{}_{}_unique", schema.name, id_prop_name);
        std::string query =
            fmt::format("CREATE UNIQUE INDEX {} FOR (n:{}) ON (n.{})", index_name, schema.name, id_prop_name);

        try {
            spdlog::info("[loader] Creating unique index '{}' on label '{}' property '{}'", index_name, schema.name,
                         id_prop_name);
            client.executeCypher(query, "default");
            spdlog::info("[loader] Unique index '{}' created successfully", index_name);
        } catch (const std::exception& e) {
            spdlog::warn("[loader] Failed to create unique index '{}': {}", index_name, e.what());
        }
    }
}

// ==================== Edge loading ====================

void loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const CsvIdMap& id_map, int batch_size,
               int concurrency) {
    EdgeSchemaMap schema_map;
    for (const auto& s : edge_schemas) {
        schema_map[s.name] = &s;
    }

    runFilesParallel(clients, edge_files.size(), concurrency, [&](shell::EuGraphRpcClient& client, size_t index) {
        loadOneEdgeFile(client, edge_files[index], schema_map, id_map, batch_size);
    });
}

void loadEdges(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const CsvIdMap& id_map, int batch_size) {
    std::vector<shell::EuGraphRpcClient*> clients{&client};
    loadEdges(clients, edge_files, edge_schemas, id_map, batch_size, 1);
}

} // namespace loader
} // namespace eugraph
