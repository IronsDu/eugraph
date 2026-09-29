#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace eugraph {
namespace loader {

/// CSV 方言。字段分隔符与引号规则由这里描述，文件本身不需要预处理。
struct CsvDialect {
    /// 字段分隔符，默认 '|'。支持 ',' / ';' / '\t' / '|'。
    char delimiter = '|';
    /// 引号字符，默认双引号；设为 '\0' 表示不启用引号解析。
    char quote = '"';
    /// 文件首字符为 UTF-8 BOM 时剥离（Excel「另存为 CSV」默认带 BOM）。
    bool strip_bom = true;
};

/// 从命令行参数解析分隔符（支持 "\\t" 字面量写法）。返回 std::nullopt 表示不支持。
bool parseDelimiter(const std::string& raw, char& out);

/// 一行 CSV：字段 + 该行在文件中的**起始**行号（1-based，含表头计算）。
/// 带引号的字段可以跨物理行，此时起始行号仍指向该记录开始的那一行。
struct CsvRow {
    std::vector<std::string> fields;
    int line_number = 0;
};

/// 一个 CSV 文件的内容。
struct CsvFile {
    std::filesystem::path path;
    /// 表头字段（未做任何 trimmed；BOM 已在读取时剥离）。
    std::vector<std::string> header;
    std::vector<CsvRow> rows;
};

/// 读取整个 CSV 文件。解析规则（RFC 4180 子集 + 实用扩展）：
///   - 引号只在该字段的第一个字符处生效（`"a,b"` 是一个字段；`a"b` 是字面量）；
///   - 引号内的分隔符与换行不切分字段；
///   - 引号内的 `""` 解转义为一个 `"`；
///   - CRLF 与 LF 都作为行结束符；CRLF 出现在字段中会保留为 CR+LF。
///
/// 失败即抛 std::runtime_error，消息含文件:行号（畸形引号、行字段数与表头不符）。
CsvFile readCsvFile(const std::filesystem::path& path, const CsvDialect& dialect);

} // namespace loader
} // namespace eugraph
