// CSV 读取能力单元测试（设计文档 §3：输入侧的硬约束）
//
// 这些用例的判据都必须在「缺陷存在时失败」：
//   R1 BOM 剥离、R2 引号解析（含引号内分隔符/换行、"" 转义）、R3 分隔符、R4 列数校验。
#include <gtest/gtest.h>

#include "program/loader/csv_reader.hpp"

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class CsvReaderTest : public ::testing::Test {
protected:
    std::filesystem::path path_;

    void SetUp() override {
        path_ =
            std::filesystem::temp_directory_path() / ("eugraph_csv_reader_test_" + std::to_string(::getpid()) + ".csv");
    }

    void TearDown() override {
        std::filesystem::remove(path_);
    }

    void write(const std::string& content) {
        std::ofstream ofs(path_, std::ios::binary | std::ios::trunc);
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    eugraph::loader::CsvFile read(char delimiter = '|') {
        eugraph::loader::CsvDialect dialect;
        dialect.delimiter = delimiter;
        return eugraph::loader::readCsvFile(path_, dialect);
    }
};

// ==================== 基础 ====================

TEST_F(CsvReaderTest, ParsesPlainFile) {
    write("id|name\n1|alice\n2|bob\n");
    auto f = read();
    EXPECT_EQ(f.header, (std::vector<std::string>{"id", "name"}));
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"1", "alice"}));
    EXPECT_EQ(f.rows[0].line_number, 2);
    EXPECT_EQ(f.rows[1].line_number, 3);
}

TEST_F(CsvReaderTest, KeepsEmptyFieldsAndTrimsNothing) {
    write("a|b|c\n1||3\n x | y |\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"1", "", "3"}));
    EXPECT_EQ(f.rows[1].fields, (std::vector<std::string>{" x ", " y ", ""}));
}

TEST_F(CsvReaderTest, HandlesLfCrLfAndMissingTrailingNewline) {
    write("a|b\r\n1|2\n3|4");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"1", "2"}));
    EXPECT_EQ(f.rows[1].fields, (std::vector<std::string>{"3", "4"}));
}

TEST_F(CsvReaderTest, SkipsBlankLines) {
    write("a|b\n1|2\n\n\n3|4\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].line_number, 2);
    EXPECT_EQ(f.rows[1].line_number, 5);
}

// ==================== R1：BOM ====================

TEST_F(CsvReaderTest, StripsUtf8BomFromHeader) {
    write("\xEF\xBB\xBFid|name\n1|alice\n");
    auto f = read();
    ASSERT_FALSE(f.header.empty());
    // 缺陷存在时首字段是 "\uFEFFid"，与配置里的 "id" 匹配不上
    EXPECT_EQ(f.header[0], "id");
    EXPECT_EQ(f.header[0].size(), 2u);
}

TEST_F(CsvReaderTest, DoesNotStripBomWhenDisabled) {
    write("\xEF\xBB\xBFid|name\n1|alice\n");
    eugraph::loader::CsvDialect dialect;
    dialect.delimiter = '|';
    dialect.strip_bom = false;
    auto f = eugraph::loader::readCsvFile(path_, dialect);
    EXPECT_EQ(f.header[0], "\xEF\xBB\xBFid");
}

// ==================== R2：引号 ====================

TEST_F(CsvReaderTest, QuotedFieldKeepsDelimiter) {
    write("a|b\n\"x|y\"|z\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 1u);
    // 缺陷存在时会被切成 3 个字段（列错位）
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"x|y", "z"}));
}

TEST_F(CsvReaderTest, QuotedFieldUnescapesDoubleQuote) {
    write("a|b\n\"say \"\"hi\"\"\"|z\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields[0], "say \"hi\"");
    EXPECT_EQ(f.rows[0].fields[1], "z");
}

TEST_F(CsvReaderTest, QuotedFieldCanContainNewline) {
    write("a|b\n\"line1\nline2\"|z\n1|2\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].fields[0], "line1\nline2");
    EXPECT_EQ(f.rows[0].line_number, 2);
    // 引号内的换行推进行号，因此下一条记录的起始行号是 4
    EXPECT_EQ(f.rows[1].fields, (std::vector<std::string>{"1", "2"}));
    EXPECT_EQ(f.rows[1].line_number, 4);
}

TEST_F(CsvReaderTest, QuoteOnlySpecialAtFieldStart) {
    write("a|b\nx\"y|z\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"x\"y", "z"}));
}

TEST_F(CsvReaderTest, EmptyQuotedField) {
    write("a|b\n\"\"|z\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"", "z"}));
}

// ==================== R3：分隔符 ====================

TEST_F(CsvReaderTest, ParsesCommaTsvAndSemicolon) {
    const std::string data = "id,name\n1,alice\n2,bob\n";
    const std::string tsv = "id\tname\n1\talice\n2\tbob\n";
    const std::string semi = "id;name\n1;alice\n2;bob\n";
    const std::string pipe = "id|name\n1|alice\n2|bob\n";

    for (const auto& [content, delim] :
         std::vector<std::pair<std::string, char>>{{data, ','}, {tsv, '\t'}, {semi, ';'}, {pipe, '|'}}) {
        write(content);
        auto f = read(delim);
        ASSERT_EQ(f.header, (std::vector<std::string>{"id", "name"})) << "delimiter=" << delim;
        ASSERT_EQ(f.rows.size(), 2u) << "delimiter=" << delim;
        EXPECT_EQ(f.rows[1].fields, (std::vector<std::string>{"2", "bob"})) << "delimiter=" << delim;
    }
}

TEST_F(CsvReaderTest, CommaDelimitedWithQuotedPipe) {
    // 逗号分隔的文件里，字段内容可以含 '|'（无需引号）；引号内可以含逗号
    write("a,b\n\"x,y\",z\n");
    auto f = read(',');
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{"x,y", "z"}));
}

TEST(ResolveDelimiterTest, AcceptsSupportedForms) {
    using eugraph::loader::resolveDelimiter;
    auto is = [](const char* raw, const std::string& expect) {
        auto got = resolveDelimiter(raw);
        return got.has_value() && *got == expect;
    };
    EXPECT_TRUE(is("|", "|"));
    EXPECT_TRUE(is(",", ","));
    EXPECT_TRUE(is(";", ";"));
    EXPECT_TRUE(is("\\t", "\t")); // 转义写法
    EXPECT_TRUE(is("tab", "\t"));
    EXPECT_TRUE(is("\t", "\t")); // 真实制表符（TSV 是合法方言）
    // 多字符分隔符（原先被 --delimiter 的单字符限制挡住）
    EXPECT_TRUE(is("::", "::"));
    EXPECT_TRUE(is("||", "||"));
    EXPECT_TRUE(is("<>", "<>"));
}

TEST(ResolveDelimiterTest, RejectsUnusableForms) {
    using eugraph::loader::resolveDelimiter;
    EXPECT_FALSE(resolveDelimiter("").has_value());
    EXPECT_FALSE(resolveDelimiter("   ").has_value()); // 纯空白无意义
    EXPECT_FALSE(resolveDelimiter(" ").has_value());   // 空格分隔符会让空字段语义不可预测
    EXPECT_FALSE(resolveDelimiter("\"").has_value());  // 与引号规则冲突
    EXPECT_FALSE(resolveDelimiter("\n").has_value());  // 与行结束符冲突
}

// 多字符分隔符必须**整体**匹配，不能被逐字符切开
TEST_F(CsvReaderTest, MultiCharDelimiterMatchesWholeToken) {
    write("a::b::c\n1::2::3\n");
    eugraph::loader::CsvDialect d;
    d.delimiter = "::";
    auto csv = eugraph::loader::readCsvFile(path_, d);
    EXPECT_EQ(csv.header, (std::vector<std::string>{"a", "b", "c"}));
    ASSERT_EQ(csv.rows.size(), 1u);
    EXPECT_EQ(csv.rows[0].fields, (std::vector<std::string>{"1", "2", "3"}));
}

// 多字符分隔符中的单个字符不应被当作分隔符
TEST_F(CsvReaderTest, MultiCharDelimiterDoesNotSplitOnPartialMatch) {
    write("a::b\n:1::2:\n");
    eugraph::loader::CsvDialect d;
    d.delimiter = "::";
    auto csv = eugraph::loader::readCsvFile(path_, d);
    ASSERT_EQ(csv.rows.size(), 1u);
    // ":1" 与 "2:" 各是一整个字段（单冒号不是分隔符）
    EXPECT_EQ(csv.rows[0].fields, (std::vector<std::string>{":1", "2:"}));
}

// ==================== R4：列数与畸形输入 ====================

TEST_F(CsvReaderTest, ReportsFieldCountMismatchWithLineNumber) {
    write("a|b|c\n1|2|3\n4|5\n");
    try {
        read();
        FAIL() << "expected field-count mismatch to throw";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find(":3:"), std::string::npos) << msg; // 行号 3
        EXPECT_NE(msg.find("expected 3 fields"), std::string::npos) << msg;
        EXPECT_NE(msg.find("got 2"), std::string::npos) << msg;
    }
}

TEST_F(CsvReaderTest, ReportsUnterminatedQuote) {
    write("a|b\n\"oops|z\n");
    try {
        read();
        FAIL() << "expected unterminated quote to throw";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("unterminated quoted field"), std::string::npos) << e.what();
    }
}

TEST_F(CsvReaderTest, ReportsEmptyFile) {
    write("");
    try {
        read();
        FAIL() << "expected empty file to throw";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("no header row"), std::string::npos) << e.what();
    }
}

// ==================== 真实语料形态（防回归） ====================

TEST_F(CsvReaderTest, RealLdbcLikeLine) {
    // LDBC post 文件的一行：content 里含逗号与句点，但没有分隔符
    write("id|imageFile|creationDate|locationIP|browserUsed|language|content|length\n"
          "618475290624||1313561140595|49.246.218.237|Firefox|uz|About Rupert Murdoch, t newer|140\n");
    auto f = read();
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields.size(), 8u);
    EXPECT_EQ(f.rows[0].fields[6], "About Rupert Murdoch, t newer");
}

} // namespace
