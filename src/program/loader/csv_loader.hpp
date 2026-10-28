#pragma once

#include "program/loader/csv_reader.hpp"
#include "program/shell/rpc_client.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace loader {

class ProgressReporter;

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
/// 一个行级标签来源。三种方式**互斥**（同时给出是配置错误，不再静默取其一）：
///   - 静态：`derived` 非空 → 直接产出这些标签，不读 CSV；
///   - 按列名：`header` → 该列每个非空值产出一个标签（表头重名时需改用 index）；
///   - 按列号：`index`（0 基）→ 用于表头不可靠/重名的文件。
struct LabelSource {
    std::string header;
    int index = -1;
    std::string case_mode = "none"; ///< none | capitalize | lower
    std::vector<std::string> derived;
};

/// 一个文件的全部行级标签来源。多个来源的结果取**并集并去重**；
/// 与主标签（schema 键）同名的标签被丢弃，主标签永不被抢占。
using LabelSpec = std::vector<LabelSource>;

/// 一个数据文件（点或边）的声明。
struct FileSpec {
    std::filesystem::path path;        ///< 解析后的绝对/相对路径
    std::string declared_file;         ///< 原样保留，供报错
    std::string element;               ///< 顶点标签名 或 边类型名（schema 的键）
    std::vector<PropertySpec> columns; ///< 属性声明（键 = 属性名）
    bool has_pk = false;
    std::vector<PropertySpec> pk; ///< 主键属性（按元组顺序；来源列在其中）
    LabelSpec row_labels;         ///< 空 = 该文件不产生行级标签
    // 仅边文件：端点列（单列主键 1 项；复合主键逐列列出，顺序与目标标签主键一致）
    std::vector<PropertySpec> src, dst;
    std::string src_label, dst_label;
    /// 文件级覆盖（缺省 = 用 SchemaConfig 的全局值）。与 columns/pk/label 同一套分层：
    /// 全局默认 → 文件级覆盖。
    std::optional<std::string> delimiter;
    std::optional<std::string> date_format;
    /// 预扫描得到的记录数（不含表头；-1 = 尚未扫描）。由 scanRowCounts 填充，
    /// 只用于进度分母与计划表，不参与数据解析。
    int64_t expected_rows = -1;
};

/// schema 里声明的一条待执行 Cypher 语句（before_load / after_load）。
struct LoadStatement {
    std::string cypher;
    bool ignore_error = false; ///< true = 失败只告警（用于重跑时 CREATE INDEX 已存在这类情况）
};

/// schema 配置（JSON）解析结果：装载入口的唯一来源。
struct SchemaConfig {
    std::filesystem::path schema_path;
    std::filesystem::path data_dir;
    /// 目标图（运行期去向，由命令行 --graph 设置，**不来自 JSON**）。
    /// 不存在时由 loadAll 自动创建；装载、DDL、语句全部作用于这张图。
    std::string graph = "default";
    /// 装载前 / 装载后要执行的 Cypher 语句（按声明顺序、单连接串行执行）。
    /// before_load 在 schema DDL 之后、任何数据写入之前；after_load 在点与边全部写完之后。
    std::vector<LoadStatement> before_load;
    std::vector<LoadStatement> after_load;
    /// 时间列的默认解析格式（全局）：epoch_ms | epoch_s | epoch_us | epoch_ns | iso。
    /// 文件级可用 FileSpec::date_format 覆盖。
    std::string date_format = "epoch_ms";
    /// 字段分隔符的全局默认值（可多字符）；文件级可用 FileSpec::delimiter 覆盖。
    std::string delimiter = "|";
    /// 数据目录里存在「未在 schema 声明」的 CSV 时的行为：
    ///   "error"（默认）→ 报错并列出文件名；"ignore" → 只记日志。
    std::string undeclared_files = "error";
    /// 明确排除的文件（相对 data_dir）：属于本数据集但不参与初始装载，
    /// 例如 LDBC 的 updateStream_*.csv（更新流，另行增量导入）。
    std::vector<std::string> ignored_files;
    std::vector<FileSpec> vertex_files;
    std::vector<FileSpec> edge_files;
    /// 标签 → 合并后的属性（顺序 = 首次出现顺序）；用于 createLabel 与属性排列
    std::unordered_map<std::string, std::vector<PropertySpec>> merged_properties;
};

/// 解析 --schema JSON 并做完整校验（文件存在性、pk 引用、端点标签、冲突等）。
/// 失败抛 std::runtime_error，消息含文件/标签/字段名。
///
/// 声明只有这一个来源：**没有命令行覆盖**（主键/类型/分隔符/时间格式全在 JSON 里），
/// 这样「同一份 schema 必然装出同一张图」，命令行历史不影响结果。
///
/// - strict_types=true（默认）：columns 未声明的列直接报错（要求显式声明）。
/// - strict_types=false：未声明的列按采样推断类型补上（前 200 行，INT64 否则 STRING）。
SchemaConfig loadSchemaConfig(const std::filesystem::path& schema_path, const std::filesystem::path& data_dir,
                              bool strict_types = true);

/// 某个文件的有效 CSV 方言 = schema 全局默认 + 文件级覆盖。
CsvDialect resolveDialect(const SchemaConfig& config, const FileSpec& fs);

/// 某个文件的有效时间解析格式 = schema 全局默认 + 文件级覆盖。
const std::string& resolveDateFormat(const SchemaConfig& config, const FileSpec& fs);

// ==================== 装载阶段 ====================

/// 预扫描每个声明文件的记录数（复用 CSV 记录切分规则，不物化字段）。
/// 结果写进 FileSpec::expected_rows，作为进度分母与计划表；畸形 CSV 在此一次性抛错
/// （发生在任何 DDL / 写入之前，不会留下半张图）。
void scanRowCounts(SchemaConfig& config);

/// 确保目标图存在（不存在则创建）。返回是否本次新建。
/// 拒绝空名与 `neo4j` / `system`：它们是服务端对 default 的别名，建出同名图会让
/// 写入与查询落到不同的图上。
bool ensureGraph(shell::EuGraphRpcClient& client, const std::string& graph_name);

/// 语句执行阶段。
enum class StatementPhase {
    BEFORE_LOAD, ///< schema DDL 之后、任何数据写入之前
    AFTER_LOAD,  ///< 点与边全部写完之后
};

/// 顺序执行 schema 里声明的语句（drain 流，确保写语句真正执行完成）。
/// 失败抛 std::runtime_error，消息含阶段、序号与语句原文；标记了 on_error=ignore 的只告警。
void runStatements(shell::EuGraphRpcClient& client, const SchemaConfig& config, StatementPhase phase);

/// 创建所有顶点标签（含主键声明）与边类型。
void createLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config);
void createEdgeLabels(shell::EuGraphRpcClient& client, const SchemaConfig& config);

/// 为每个声明了主键的标签创建覆盖全部主键列的复合唯一索引（幂等）。
void createPrimaryKeyIndexes(shell::EuGraphRpcClient& client, const SchemaConfig& config);

/// 装载全部顶点文件。返回 {写入顶点数, 主键重复跳过数}。
std::pair<int64_t, int64_t> loadVertices(const std::vector<shell::EuGraphRpcClient*>& clients,
                                         const SchemaConfig& config, int batch_size, int concurrency,
                                         ProgressReporter* progress = nullptr);

/// 装载全部边文件。返回 {写入边数, 端点未解析跳过数}。
std::pair<int64_t, int64_t> loadEdges(const std::vector<shell::EuGraphRpcClient*>& clients, const SchemaConfig& config,
                                      int batch_size, int concurrency, ProgressReporter* progress = nullptr);

/// 统一装载管线的一次运行参数。
struct LoadOptions {
    int batch_size = 500;
    int parallel_files = 1;
    ProgressReporter* progress = nullptr; ///< 可空；非空时输出计划表与进度
};

/// 统一装载管线的一次运行结果。
struct LoadResult {
    int64_t vertices_written = 0;
    int64_t vertices_duplicate = 0;
    int64_t edges_written = 0;
    int64_t edges_skipped = 0;
};

/// 唯一装载管线（loader_main 与集成测试共用，保证「执行顺序」本身被测试覆盖）：
///   目标图 → schema DDL（标签/边类型/主键索引）→ before_load → 点 → 边 → after_load
/// 开始前先预扫描行数并打印装载计划；config 由本函数填充 expected_rows（故取非 const）。
LoadResult loadAll(const std::vector<shell::EuGraphRpcClient*>& clients, SchemaConfig& config,
                   const LoadOptions& options);

/// 校验：数据目录下的 CSV 是否都在 schema 里声明过（未声明的文件多半是配置漏写）。
/// 返回未声明的文件列表（相对 data_dir）。
std::vector<std::string> findUndeclaredCsvFiles(const SchemaConfig& config);

} // namespace loader
} // namespace eugraph
