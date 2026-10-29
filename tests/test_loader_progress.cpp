// Loader 进度 / 计划表单元测试（不需要 server）。
//
// 判据（缺陷存在时必须失败）：
//   - 计划表漏掉任何标签/边类型，或行数不是预扫描值 → 使用者看不到「要装什么」；
//   - 并行装载同一标签的多个文件时计数丢失 → 总进度偏小；
//   - 非 TTY 输出 ANSI 控制符 → 日志/CI 被污染；
//   - OFF 模式仍然输出 → 脚本无法安静运行。
#include <gtest/gtest.h>

#include "program/loader/loader_progress.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace eugraph::loader;

namespace {

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines)
        out += line + "\n";
    return out;
}

/// 去掉 ANSI 转义：内容断言必须在"可见文本"上做 —— 进度条的 `==` 与 `>` 是两段不同颜色，
/// 原始串里并不存在连续的 "==>"（这条曾让用例只在彩色终端/CI 上失败）。
std::string stripAnsi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= 0x40 && text[i] <= 0x7e))
                ++i;
            if (i < text.size())
                ++i;
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

} // namespace

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        if (end == std::string::npos) {
            if (begin < text.size())
                lines.push_back(text.substr(begin));
            break;
        }
        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

/// 可见列数：CSI 序列与 UTF-8 续字节不占列，与 loader_progress.cpp 的实现同口径。
std::size_t visibleWidth(std::string_view text) {
    std::size_t columns = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= 0x40 && text[i] <= 0x7e))
                ++i;
            if (i < text.size())
                ++i;
            continue;
        }
        ++i;
        if ((c & 0xC0U) != 0x80U)
            ++columns;
    }
    return columns;
}

/// 每个 CSI 序列都必须有终止字节：被字节截断的行会出现半个 ESC[...，终端会吞掉后续内容。
bool ansiWellFormed(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\x1b')
            continue;
        if (i + 1 >= text.size() || text[i + 1] != '[')
            continue;
        std::size_t end = i + 2;
        while (end < text.size() && !(text[end] >= 0x40 && text[end] <= 0x7e))
            ++end;
        if (end >= text.size())
            return false;
        i = end;
    }
    return true;
}

} // namespace

TEST(LoadPlanFormatTest, ListsEveryNodeAndEdgeWithRowTotals) {
    LoadPlan plan;
    plan.graph = "g2";
    plan.nodes = {{"Person", 2, 9890, ""}, {"Place", 1, 1460, ""}};
    plan.edges = {{"KNOWS", 1, 180623, "Person -> Person"}};

    const std::string text = joinLines(formatPlanLines(plan));
    EXPECT_NE(text.find("g2"), std::string::npos) << text;
    for (const char* name : {"Person", "Place", "KNOWS"})
        EXPECT_NE(text.find(name), std::string::npos) << name << "\n" << text;
    // 行数必须逐个可核对，且汇总行是两个标签之和（9890 + 1460 = 11,350）
    EXPECT_NE(text.find("9,890"), std::string::npos) << text;
    EXPECT_NE(text.find("1,460"), std::string::npos) << text;
    EXPECT_NE(text.find("180,623"), std::string::npos) << text;
    EXPECT_NE(text.find("11,350"), std::string::npos) << text;
    // 边的端点标签也要写清楚
    EXPECT_NE(text.find("Person -> Person"), std::string::npos) << text;
}

TEST(LoadPlanFormatTest, ReportsFileCounts) {
    LoadPlan plan;
    plan.graph = "default";
    plan.nodes = {{"Person", 2, 10, ""}};
    const std::string text = joinLines(formatPlanLines(plan));
    EXPECT_NE(text.find("2 files"), std::string::npos) << text;
}

TEST(ProgressAccountingTest, AggregatesParallelFilesPerItem) {
    std::ostringstream out;
    ProgressReporter progress(ProgressMode::OFF, out);
    progress.beginPhase("vertices", "labels", {{"Person", 2, 200, ""}, {"Place", 1, 50, ""}});
    progress.beginFile("Person", "a.csv", 100);
    progress.beginFile("Person", "b.csv", 100);
    progress.beginFile("Place", "p.csv", 50);

    // 同一标签的两个文件并发推进（--parallel-files>1）：两边的计数都必须计入
    std::thread t1([&] {
        for (int i = 0; i < 100; ++i)
            progress.addRows("Person", "a.csv", 1);
    });
    std::thread t2([&] {
        for (int i = 0; i < 100; ++i)
            progress.addRows("Person", "b.csv", 1);
    });
    t1.join();
    t2.join();

    EXPECT_EQ(progress.itemRowsDone("Person"), 200);
    EXPECT_EQ(progress.itemRowsDone("Place"), 0);
    EXPECT_EQ(progress.phaseRowsDone(), 200);
    EXPECT_EQ(progress.phaseRowsTotal(), 250);

    progress.addRows("Place", "p.csv", 50);
    EXPECT_EQ(progress.phaseRowsDone(), 250);

    progress.endFile("Person", "a.csv", 100, 0);
    progress.endFile("Person", "b.csv", 100, 0);
    progress.endFile("Place", "p.csv", 50, 0);
    progress.endPhase();

    EXPECT_TRUE(out.str().empty()) << "OFF 模式不得输出任何内容，实际输出：\n" << out.str();
}

TEST(ProgressPlainModeTest, EmitsPlanFileLinesAndPhaseSummary) {
    std::ostringstream out;
    ProgressReporter progress(ProgressMode::PLAIN, out);

    LoadPlan plan;
    plan.graph = "default";
    plan.nodes = {{"Person", 1, 2, ""}};
    progress.printPlan(plan);

    progress.beginPhase("vertices", "labels", plan.nodes);
    progress.beginFile("Person", "person.csv", 2);
    progress.addRows("Person", "person.csv", 2);
    progress.endFile("Person", "person.csv", 2, 0);
    progress.endPhase();

    const std::string text = out.str();
    EXPECT_NE(text.find("Person"), std::string::npos) << text;
    EXPECT_NE(text.find("person.csv"), std::string::npos) << text;
    EXPECT_NE(text.find("vertices done"), std::string::npos) << text;
    // 非 TTY：绝不能出现 ANSI 控制符（CI 日志里会变成乱码）
    EXPECT_EQ(text.find('\x1b'), std::string::npos) << "非 TTY 输出里出现了 ANSI 控制符";
}

TEST(ProgressPlainModeTest, SkipsFailedFileWithSkipCount) {
    std::ostringstream out;
    ProgressReporter progress(ProgressMode::PLAIN, out);
    progress.beginPhase("edges", "types", {{"KNOWS", 1, 10, "Person -> Person"}});
    progress.beginFile("KNOWS", "knows.csv", 10);
    progress.addRows("KNOWS", "knows.csv", 10);
    // 10 条里 8 条写入、2 条端点未解析：完成行要同时给出两个口径
    progress.endFile("KNOWS", "knows.csv", 8, 2);
    progress.endPhase();

    const std::string text = out.str();
    EXPECT_NE(text.find("written 8"), std::string::npos) << text;
    EXPECT_NE(text.find("skipped 2"), std::string::npos) << text;
}

// ==================== 原地刷新块（force_interactive）====================

TEST(ProgressInteractiveTest, BlockShowsOverallBarFileBarAndCounts) {
    std::ostringstream out;
    {
        ProgressReporter progress(ProgressMode::AUTO, out, /*force_interactive=*/true);
        progress.beginPhase("vertices", "labels", {{"Person", 1, 200, ""}});
        progress.beginFile("Person", "person.csv", 200);
        progress.addRows("Person", "person.csv", 50);
        progress.flush(); // 立即重绘，不依赖 ticker 的 100ms 时序
    }
    const std::string text = out.str();
    ASSERT_FALSE(text.empty());

    EXPECT_TRUE(ansiWellFormed(text)) << "存在被切断的 ANSI 序列（会把整行吞掉）";
    for (const auto& line : splitLines(text))
        EXPECT_LE(visibleWidth(line), 118) << "行超宽会导致终端自动换行、原地重绘错位:\n" << line;

    // 内容断言在剥掉 ANSI 之后做：`==` 与 `>` 是两段不同颜色，原始串里没有连续的 "==>"
    const std::string plain = stripAnsi(text);
    EXPECT_NE(plain.find("==>"), std::string::npos) << "进度条箭头缺失\n" << plain;
    EXPECT_NE(plain.find("25%"), std::string::npos) << "百分比与 50/200 不一致\n" << plain;
    EXPECT_NE(plain.find("50/200"), std::string::npos) << "已处理/总数缺失\n" << plain;
    EXPECT_NE(text.find("?25l"), std::string::npos) << "没有隐藏光标";
    EXPECT_NE(text.find("?25h"), std::string::npos) << "析构没有恢复光标";
}

TEST(ProgressInteractiveTest, LongFileNamesKeepBarCountsAndWidth) {
    // 长文件名过去会把整行顶到 118 字节以上，再按字节截断 → 行尾计数/速率被砍掉
    const std::string long_name(160, 'x');
    const std::string file = long_name + ".csv";
    std::ostringstream out;
    {
        ProgressReporter progress(ProgressMode::AUTO, out, /*force_interactive=*/true);
        progress.beginPhase("edges", "types", {{"HAS_INTEREST", 1, 1000, "Person -> Tag"}});
        progress.beginFile("HAS_INTEREST", file, 1000);
        progress.addRows("HAS_INTEREST", file, 500);
        progress.flush();
    }
    const std::string text = out.str();
    EXPECT_TRUE(ansiWellFormed(text));
    for (const auto& line : splitLines(text))
        EXPECT_LE(visibleWidth(line), 118) << line;
    const std::string plain = stripAnsi(text);
    EXPECT_NE(plain.find("500/1,000"), std::string::npos) << "长文件名把行尾计数挤掉了\n" << plain;
    EXPECT_NE(plain.find("50%"), std::string::npos) << plain;
}

TEST(ProgressInteractiveTest, CompletedPhaseShowsFullBarAndHundredPercent) {
    std::ostringstream out;
    {
        ProgressReporter progress(ProgressMode::AUTO, out, /*force_interactive=*/true);
        progress.beginPhase("vertices", "labels", {{"Person", 1, 10, ""}});
        progress.beginFile("Person", "person.csv", 10);
        progress.addRows("Person", "person.csv", 10);
        progress.endFile("Person", "person.csv", 10, 0);
        progress.endPhase();
    }
    const std::string text = out.str();
    EXPECT_TRUE(ansiWellFormed(text));
    const std::string plain = stripAnsi(text);
    EXPECT_NE(plain.find("100%"), std::string::npos) << plain;
    EXPECT_NE(plain.find("======"), std::string::npos) << "完成时进度条应被填满\n" << plain;
}
