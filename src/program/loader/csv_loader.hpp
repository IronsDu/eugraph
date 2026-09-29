#pragma once

#include "program/loader/csv_reader.hpp"
#include "program/shell/rpc_client.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace loader {

/// CSV 列类型。与 thrift PropertyType 一一对应；DATETIME/TIME/DURATION 解析为
/// 对应的 temporal 值（kind 由类型名决定），数组类型按 ';' 拆分。
enum class CsvColumnType {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    BOOL_ARRAY,
    INT64_ARRAY,
    DOUBLE_ARRAY,
    STRING_ARRAY,
    DATE,
    DATETIME,
    DATETIME_WITH_TZ,
    TIME,
    TIME_WITH_TZ,
    DURATION,
    DATE_ARRAY,
    DATETIME_ARRAY,
    TIME_ARRAY,
    DURATION_ARRAY,
};

/// 一个属性的声明：属性名 + 来源列（按列名或列号）+ 类型。
struct PropertySpec {
    std::string name;   ///< 图里的属性名（columns 的键）
    std::string header; ///< CSV 列名（空 = 用 index 定位）
    int index = -1;     ///< 0-based 列号（header 为空时使用）
    CsvColumnType type = CsvColumnType::STRING;
    bool type_declared = false;
    int column = -1; ///< 解析后：实际列号（-1 = 尚未解析）
};

/// 行级标签来源（追加标签，不抢主标签）。
struct LabelSource {
    std::string header;
    int index = -1;
    std::string case_mode = "none"; ///< none | capitalize | lower
    std::vector<std::string> derived;
};

/// 一个数据文件（点或边）的声明。
struct FileSpec {
    std::filesystem::path path;        ///< 解析后的绝对/相对路径
    std::string declared_file;         ///< 原样保留，供报错
    std::string element;               ///< 顶点标签名 或 边类型名（schema 的键）
    std::vector<PropertySpec> columns; ///< 属性声明（键 = 属性名）
    bool has_pk = false;
    std::vector<PropertySpec> pk; ///< 主键属性（按元组顺序；来源列在其中）
    std::optional<LabelSource> row_label;
    // 仅边文件：端点列（单列主键 1 项；复合主键逐列列出，顺序与目标标签主键一致）
    std::vector<PropertySpec> src, dst;
    std::string src_label, dst_label;
};

/// schema 配置（JSON）解析结果：装载入口的唯一来源。
struct SchemaConfig {
    std::filesystem::path schema_path;
    std::filesystem::path data_dir;
    std::string date_format = "epoch_ms"; ///< epoch_ms | epoch_s | epoch_us | epoch_ns | iso
    /// 明确排除的文件（相对 data_dir）：属于本数据集但不参与初始装载，
    /// 例如 LDBC 的 updateStream_*.csv（更新流，另行增量导入）。
    std::vector<std::string> ignored_files;
    std::vector<FileSpec> vertex_files;
    std::vector<FileSpec> edge_files;
    /// 标签 → 合并后的属性（顺序 = 首次出现顺序）；用于 createLabel 与属性排列
    std::unordered_map<std::string, std::vector<PropertySpec>> merged_properties;
};

/// CLI 覆盖（优先级高于 schema 文件；键的含义与 JSON 一致）：
///   pk[label]        = 该标签的主键【属性名】（顺序即元组顺序；复合主键用逗号分隔）
///   types[label][p]  = 该标签属性 p 的类型（键是属性名；不存在则按 p 为属性名、列名同名新增）
///   props[type][p]   = 该边类型属性 p 的类型（同上）
///   date_format      = 覆盖 schema 的 date_format
struct CliOverrides {
    std::unordered_map<std::string, std::vector<std::string>> pk;                        // label -> pk 属性名
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> types; // label/type -> prop -> type
    std::optional<std::string> date_format;
};

/// 解析 --schema JSON 并做完整校验（文件存在性、pk 引用、端点标签、冲突等）。
/// 失败抛 std::runtime_error，消息含文件/标签/字段名。
/// - strict_types=true（默认）：columns 未声明的列直接报错（要求显式声明）。
/// - strict_types=false：未声明的列按采样推断类型补上（前 200 行，INT64 否则 STRING）。
/// - overrides 非空时先应用到配置上再做校验。
SchemaConfig loadSchemaConfig(const std::filesystem::path& schema_path, const std::filesystem::path& data_dir,
                              const CsvDialect& dialect = CsvDialect{}, bool strict_types = true,
                              const CliOverrides* overrides = nullptr);

// ==================== 装载阶段 ====================

/// 创建所有顶点标签（含主键声明）与边类型。
void createLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config);
void createEdgeLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config);

/// 为每个声明了主键的标签创建覆盖全部主键列的复合唯一索引（幂等）。
void createPrimaryKeyIndexes(shell::EuGraphRpcClient& client, const SchemaConfig& config);

/// 装载全部顶点文件。返回 {写入顶点数, 主键重复跳过数}。
std::pair<int64_t, int64_t> loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                                         const SchemaConfig& config, const CsvDialect& dialect, int batch_size,
                                         int concurrency);

/// 装载全部边文件。返回 {写入边数, 端点未解析跳过数}。
std::pair<int64_t, int64_t> loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const SchemaConfig& config,
                                      const CsvDialect& dialect, int batch_size, int concurrency);

/// 校验：数据目录下的 CSV 是否都在 schema 里声明过（未声明的文件多半是配置漏写）。
/// 返回未声明的文件列表（相对 data_dir）。
std::vector<std::string> findUndeclaredCsvFiles(const SchemaConfig& config);

} // namespace loader
} // namespace eugraph
