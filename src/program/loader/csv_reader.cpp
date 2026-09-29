#include "program/loader/csv_reader.hpp"

#include <cstdio>
#include <fstream>
#include <optional>
#include <stdexcept>

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

} // namespace

bool parseDelimiter(const std::string& raw, char& out) {
    if (raw == "\\t" || raw == "tab") {
        out = '\t';
        return true;
    }
    if (raw.size() != 1)
        return false;
    switch (raw[0]) {
    case ',':
    case ';':
    case '|':
    case '\t':
        out = raw[0];
        return true;
    default:
        return false;
    }
}

CsvFile readCsvFile(const std::filesystem::path& path, const CsvDialect& dialect) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("cannot open CSV file: " + path.string());

    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (dialect.strip_bom && data.size() >= kUtf8BomLen && data.compare(0, kUtf8BomLen, kUtf8Bom) == 0) {
        data.erase(0, kUtf8BomLen);
    }

    CsvFile out;
    out.path = path;

    const char delim = dialect.delimiter;
    const char quote = dialect.quote;
    const bool quoting = quote != '\0';

    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    bool field_started = false; // 本字段是否已经消费过字符（用于「引号只在字段首字符生效」）
    bool row_started = false;   // 本行是否已有内容（用于忽略文件末尾的空行）
    int line = 1;               // 当前物理行
    int row_line = 1;           // 当前记录起始物理行

    auto end_field = [&]() {
        fields.push_back(std::move(field));
        field.clear();
        field_started = false;
    };

    auto end_row = [&]() {
        end_field();
        if (out.header.empty()) {
            out.header = std::move(fields);
        } else {
            if (fields.size() != out.header.size()) {
                fail(path, row_line,
                     "field count mismatch: " + formatExpectedActual(out.header.size(), fields.size()) +
                         " (if a field contains the delimiter, quote it or change --delimiter)");
            }
            out.rows.push_back(CsvRow{std::move(fields), row_line});
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

        if (c == delim) {
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

    if (out.header.empty())
        throw std::runtime_error("empty CSV file (no header row): " + path.string());

    return out;
}

} // namespace loader
} // namespace eugraph
