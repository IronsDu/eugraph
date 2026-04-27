#pragma once

#include "common/types/graph_types.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "shell/rpc_client.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace loader {

struct CsvFileInfo {
    std::filesystem::path path;
    bool is_vertex;
    std::string label;
    std::string src_label;
    std::string edge_type;
    std::string dst_label;
};

struct PropertyInfo {
    std::string name;
    bool is_int64;
};

struct LabelSchema {
    std::string name;
    std::vector<PropertyInfo> properties; // excludes id column
};

struct EdgeTypeSchema {
    std::string name;
    std::vector<PropertyInfo> properties; // excludes src/dst id columns
};

using CsvIdMap = std::unordered_map<std::string, std::unordered_map<int64_t, uint64_t>>;

// Scan directory for CSV files, classify into vertex/edge files.
std::vector<CsvFileInfo> scanCsvFiles(const std::string& data_dir);

// Parse a CSV line with pipe delimiter.
std::vector<std::string> parseCsvLine(const std::string& line);

// Build label schemas from vertex files.
std::vector<LabelSchema> buildLabelSchemas(const std::vector<CsvFileInfo>& vertex_files);

// Build edge type schemas from edge files (merges duplicate edge types).
std::vector<EdgeTypeSchema> buildEdgeTypeSchemas(const std::vector<CsvFileInfo>& edge_files);

// Infer property types from CSV content (sampling first N rows).
PropertyInfo inferPropertyType(const std::string& name, const std::vector<std::string>& samples);

// Convert a string value to thrift PropertyValueThrift.
thrift::PropertyValueThrift toThriftValue(const std::string& value, bool is_int64);

// Create labels on server via RPC.
void createLabels(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas);

// Create edge labels on server via RPC.
void createEdgeLabels(shell::EuGraphRpcClient& client, const std::vector<EdgeTypeSchema>& schemas);

// Load all vertex files, return CSV ID mapping.
CsvIdMap loadVertices(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& vertex_files,
                      const std::vector<LabelSchema>& label_schemas, int batch_size);

// Load all edge files using the CSV ID mapping.
void loadEdges(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const CsvIdMap& id_map, int batch_size);

} // namespace loader
} // namespace eugraph
