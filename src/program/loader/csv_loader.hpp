#pragma once

#include "common/types/graph_types.hpp"
#include "program/shell/rpc_client.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace loader {

enum class CsvColumnType {
    INT64,
    DOUBLE,
    STRING,
    BOOL,
    STRING_ARRAY,
    INT64_ARRAY,
    DOUBLE_ARRAY,
};

struct CsvFileInfo {
    std::filesystem::path path;
    bool is_vertex;
    // Legacy directory-scan fields.
    std::string label;
    std::string src_label;
    std::string edge_type;
    std::string dst_label;
    // CLI-mode vertex file labels. Empty means use `label`.
    std::vector<std::string> labels;
};

struct PropertyInfo {
    std::string name;
    CsvColumnType type;
};

struct LabelSchema {
    std::string file_key;                 // unique key for this vertex file
    std::string name;                     // default primary label (first file-level label)
    std::vector<std::string> labels;      // file-level labels (first is primary)
    std::string group;                    // ID space / group name
    std::vector<PropertyInfo> properties; // excludes id and :LABEL columns
    int id_col = 0;                       // 0-based CSV column of the node id
    int label_col = -1;                   // 0-based CSV column of :LABEL, -1 if absent
};

struct EdgeTypeSchema {
    std::string name;      // edge type
    std::string src_label; // src group label (resolved via label_to_group)
    std::string dst_label; // dst group label
    std::vector<PropertyInfo> properties;
    int src_col = 0;
    int dst_col = 1;
};

using CsvIdMap = std::unordered_map<std::string, std::unordered_map<int64_t, uint64_t>>;

struct LoadedIdMaps {
    CsvIdMap group_id_map;                                       // group -> (csv_id -> vertex_id)
    std::unordered_map<std::string, std::string> label_to_group; // label -> group
};

// Scan directory for CSV files, classify into vertex/edge files.
std::vector<CsvFileInfo> scanCsvFiles(const std::string& data_dir);

// Parse a CSV line with pipe delimiter.
std::vector<std::string> parseCsvLine(const std::string& line);

// Parse a typed header token (e.g. "name:STRING", "length:INT").
PropertyInfo parseTypedColumn(const std::string& token);

// Convert a CSV value to thrift PropertyValueThrift according to column type.
thrift_service::PropertyValueThrift toThriftValue(const std::string& value, CsvColumnType type);

// Build label schemas from vertex files (reads headers + samples).
std::vector<LabelSchema> buildLabelSchemas(const std::vector<CsvFileInfo>& vertex_files);

// Merge property definitions per label across vertex files. The returned
// order is the order in which properties are declared on the server label.
std::unordered_map<std::string, std::vector<PropertyInfo>>
buildMergedLabelProperties(const std::vector<LabelSchema>& schemas);

// Build edge type schemas from edge files (reads headers + samples).
std::vector<EdgeTypeSchema> buildEdgeTypeSchemas(const std::vector<CsvFileInfo>& edge_files);

// Create labels on server via RPC.
void createLabels(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas);

// Create edge labels on server via RPC.
void createEdgeLabels(shell::EuGraphRpcClient& client, const std::vector<EdgeTypeSchema>& schemas);

// Load all vertex files, return CSV ID mapping and label->group mapping.
LoadedIdMaps loadVertices(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& vertex_files,
                          const std::vector<LabelSchema>& label_schemas,
                          const std::unordered_map<std::string, std::vector<PropertyInfo>>& merged_label_props,
                          int batch_size);

// Load all vertex files with a pool of RPC clients (each owns an EventBase) and bounded concurrency.
LoadedIdMaps loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                          const std::vector<CsvFileInfo>& vertex_files, const std::vector<LabelSchema>& label_schemas,
                          const std::unordered_map<std::string, std::vector<PropertyInfo>>& merged_label_props,
                          int batch_size, int concurrency);

// Create unique indexes on ID properties for all labels (after vertices loaded).
void createUniqueIdIndexes(shell::EuGraphRpcClient& client, const std::vector<LabelSchema>& schemas);

// Load all edge files using the CSV ID mapping.
void loadEdges(shell::EuGraphRpcClient& client, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const LoadedIdMaps& id_maps, int batch_size);

// Load all edge files with a pool of RPC clients (each owns an EventBase) and bounded concurrency.
void loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const std::vector<CsvFileInfo>& edge_files,
               const std::vector<EdgeTypeSchema>& edge_schemas, const LoadedIdMaps& id_maps, int batch_size,
               int concurrency);

// Parse --nodes/--relationships spec strings. Returns false on syntax error.
bool parseNodeSpec(const std::string& spec, CsvFileInfo& info, std::string& error);
bool parseRelationshipSpec(const std::string& spec, CsvFileInfo& info, std::string& error);

} // namespace loader
} // namespace eugraph
