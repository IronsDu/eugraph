// 预扫描计数复用仓库内 vendored 的 csv.h（Ben Strasser）的 io::LineReader：
// 按 ~1MB 块顺序扫描、只找换行，比逐字符解析快一个量级。
// CSV_IO_NO_THREAD：不启用它的后台读线程 —— loader 本身已按文件多线程并行，
// 再为每个文件开一条读线程只会放大线程数。
#define CSV_IO_NO_THREAD
#include "csv.h"

#include "program/loader/csv_reader.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace eugraph {
namespace loader {

namespace {

constexpr const char* kUtf8Bom = "\xEF\xBB\xBF";
constexpr size_t kUtf8BomLen = 3;

[[noreturn]] void fail(const std::filesystem::path& path, int line, const std::string& what) {
    throw std::runtime_error(path.string() + ":" + std::to_string(line) + ": " + what);
}

std::string formatExpectedActual(size_t expected, size_t actual) {
    return "expected " + std::to_string(expected) + " fields (per header), got " + std::to_string(actual);
}

/// 记录切分内核：readCsvFile 与 countCsvRecords 共用同一份规则。
/// 两个入口必须永远看到同一组记录，否则「进度分母」与「实际装载行数」会漂移。
using HeaderSink = std::function<void(std::vector<std::string>&&)>;
using RowSink = std::function<void(std::vector<std::string>&&, int)>;

void parseCsv(const std::filesystem::path& path, const CsvDialect& dialect, const HeaderSink& on_header,
              const RowSink& on_row) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("cannot open CSV file: " + path.string());

    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (dialect.strip_bom && data.size() >= kUtf8BomLen && data.compare(0, kUtf8BomLen, kUtf8Bom) == 0) {
        data.erase(0, kUtf8BomLen);
    }

    const std::string delim = dialect.delimiter.empty() ? std::string("|") : dialect.delimiter;
    const char quote = dialect.quote;
    const bool quoting = quote != '\0';

    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    bool field_started = false; // 本字段是否已经消费过字符（用于「引号只在字段首字符生效」）
    bool row_started = false;   // 本行是否已有内容（用于忽略文件末尾的空行）
    bool have_header = false;
    size_t header_size = 0;
    int line = 1;     // 当前物理行
    int row_line = 1; // 当前记录起始物理行

    auto end_field = [&]() {
        fields.push_back(std::move(field));
        field.clear();
        field_started = false;
    };

    auto end_row = [&]() {
        end_field();
        if (!have_header) {
            have_header = true;
            header_size = fields.size();
            on_header(std::move(fields));
        } else {
            if (fields.size() != header_size) {
                fail(path, row_line,
                     "field count mismatch: " + formatExpectedActual(header_size, fields.size()) +
                         " (if a field contains the delimiter '" + delim +
                         "', quote it or change the schema's "
                         "delimiter)");
            }
            on_row(std::move(fields), row_line);
        }
        fields.clear();
        row_started = false;
    };

    for (size_t i = 0; i < data.size(); ++i) {
        const char c = data[i];

        if (in_quotes) {
            if (c == quote) {
                if (quoting && i + 1 < data.size() && data[i + 1] == quote) {
                    field.push_back(quote); // "" -> "
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                if (c == '\n')
                    ++line; // 引号内的换行属于字段内容，但仍要推进行号
                field.push_back(c);
            }
            continue;
        }

        if (quoting && c == quote && !field_started) {
            in_quotes = true;
            field_started = true;
            row_started = true;
            continue;
        }

        // 分隔符按**整体**匹配（支持多字符），不能逐字符比较
        if (c == delim[0] && data.compare(i, delim.size(), delim) == 0) {
            i += delim.size() - 1;
            end_field();
            row_started = true;
            continue;
        }

        if (c == '\n') {
            if (row_started || !field.empty() || !fields.empty()) {
                end_row();
            } else {
                // 空行：跳过（不计入数据行）
            }
            ++line;
            row_line = line;
            continue;
        }

        if (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n')
            continue; // CRLF：由随后的 '\n' 结束本行

        field.push_back(c);
        field_started = true;
        row_started = true;
    }

    if (in_quotes)
        fail(path, row_line, "unterminated quoted field (reached end of file)");
    if (row_started || !field.empty() || !fields.empty())
        end_row();

    if (!have_header)
        throw std::runtime_error("empty CSV file (no header row): " + path.string());
}

} // namespace

std::optional<std::string> resolveDelimiter(std::string_view raw) {
    if (raw == "\\t" || raw == "tab")
        return std::string("\t");
    if (raw.empty())
        return std::nullopt;
    // 制表符是合法分隔符（TSV），必须先于「纯空白」规则接受
    if (raw == "\t")
        return std::string("\t");
    // 其它纯空白（空格等）没有意义：会让「空字段」的语义无法预测
    if (raw.find_first_not_of(" \r\n\t") == std::string_view::npos)
        return std::nullopt;
    // 引号与换行与解析规则本身冲突
    if (raw.find('"') != std::string_view::npos || raw.find('\n') != std::string_view::npos ||
        raw.find('\r') != std::string_view::npos)
        return std::nullopt;
    return std::string(raw);
}

CsvFile readCsvFile(const std::filesystem::path& path, const CsvDialect& dialect) {
    CsvFile out;
    out.path = path;
    parseCsv(
        path, dialect, [&out](std::vector<std::string>&& header) { out.header = std::move(header); },
        [&out](std::vector<std::string>&& fields, int line) { out.rows.push_back(CsvRow{std::move(fields), line}); });
    return out;
}

namespace {

/// 快速计数：文件里**没有引号字符**时，记录数 = 非空物理行数 − 1（表头）。
///
/// 与 parseCsv 的语义对齐点：空行不计记录、CRLF 与 LF 都算行结束、BOM 由 LineReader 剥离。
/// 一旦某行出现引号字符（引号字段可能跨行，物理行数 ≠ 记录数）或行超过 LineReader 的
/// 长度上限，返回 nullopt 交给 parseCsv 精确计数 —— 快速路径只做「确定安全」的那部分。
std::optional<std::size_t> tryCountByPhysicalLines(const std::filesystem::path& path, const CsvDialect& dialect) {
    const std::string delim = dialect.delimiter.empty() ? std::string("|") : dialect.delimiter;
    auto field_count = [&delim](std::string_view line) {
        // 没有引号时：字段数 = 1 + 分隔符出现次数（分隔符按整体匹配，与 parseCsv 一致）
        std::size_t fields = 1;
        for (std::size_t pos = line.find(delim); pos != std::string_view::npos;
             pos = line.find(delim, pos + delim.size()))
            ++fields;
        return fields;
    };
    try {
        io::LineReader reader(path.string());
        std::size_t records = 0;
        std::size_t header_fields = 0;
        int line_number = 0;
        bool header_seen = false;
        while (const char* line = reader.next_line()) {
            ++line_number;
            if (dialect.quote != '\0' && std::strchr(line, dialect.quote) != nullptr)
                return std::nullopt; // 引号字段可能跨行：不能按物理行计数
            if (!header_seen) {
                header_seen = true;
                header_fields = field_count(line);
                continue; // 表头
            }
            if (*line == '\0')
                continue; // 空行不计记录（parseCsv 同样跳过、也不校验列数）
            const std::size_t fields = field_count(line);
            if (fields != header_fields) {
                // 与 parseCsv 完全相同的报错：畸形 CSV 在预扫描阶段就暴露
                fail(path, line_number,
                     "field count mismatch: " + formatExpectedActual(header_fields, fields) +
                         " (if a field contains the delimiter '" + delim +
                         "', quote it or change the schema's delimiter)");
            }
            ++records;
        }
        if (!header_seen)
            throw std::runtime_error("empty CSV file (no header row): " + path.string());
        return records;
    } catch (const io::error::line_length_limit_exceeded&) {
        return std::nullopt; // 超长行：parseCsv 没有行长限制，用它精确计数
    } catch (const io::error::can_not_open_file&) {
        throw std::runtime_error("cannot open CSV file: " + path.string());
    }
}

} // namespace

std::size_t countCsvRecords(const std::filesystem::path& path, const CsvDialect& dialect) {
    if (const auto fast = tryCountByPhysicalLines(path, dialect))
        return *fast;
    std::size_t count = 0;
    parseCsv(path, dialect, [](std::vector<std::string>&&) {}, [&count](std::vector<std::string>&&, int) { ++count; });
    return count;
}

} // namespace loader
} // namespace eugraph
