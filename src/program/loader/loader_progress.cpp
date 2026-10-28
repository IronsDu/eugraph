#include "program/loader/loader_progress.hpp"

#include <spdlog/spdlog.h>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>

namespace eugraph {
namespace loader {

namespace {

constexpr std::size_t kOverallBarWidth = 28; ///< 总进度条宽度（字符数）
constexpr std::size_t kFileBarWidth = 18;    ///< 单文件进度条宽度（字符数）
constexpr std::size_t kMaxBlockLines = 24;   ///< 进度块上限：超出的完成行挤成普通行，块保持有界
constexpr std::size_t kMaxLineColumns = 118; ///< 单行可见列上限，避免终端自动换行破坏原地重绘

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kHideCursor = "\x1b[?25l";
constexpr const char* kShowCursor = "\x1b[?25h";
constexpr const char* kFrameColor = "\x1b[38;5;240m"; ///< 256 色：进度条外框固定深灰

/// 千分位计数：327,588。
std::string formatCount(int64_t value) {
    const bool negative = value < 0;
    const std::string digits = std::to_string(negative ? -value : value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    int n = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (n > 0 && n % 3 == 0)
            out.push_back(',');
        out.push_back(*it);
        ++n;
    }
    if (negative)
        out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

std::string padRight(std::string text, std::size_t width) {
    if (text.size() < width)
        text.append(width - text.size(), ' ');
    return text;
}

std::string padLeft(const std::string& text, std::size_t width) {
    if (text.size() >= width)
        return text;
    return std::string(width - text.size(), ' ') + text;
}

/// 截断到 max_bytes，且不切开 UTF-8 多字节字符（进度条里的 '·' 是 2 字节）。
std::string truncateUtf8(const std::string& text, std::size_t max_bytes) {
    if (text.size() <= max_bytes)
        return text;
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U)
        --cut;
    return text.substr(0, cut);
}

/// 可见列数：CSI 转义序列与 UTF-8 续字节都不占列（用于校验/截断带颜色的行）。
std::size_t visibleColumns(std::string_view text) {
    std::size_t columns = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= 0x40 && text[i] <= 0x7e))
                ++i;
            if (i < text.size())
                ++i; // 终止字节
            continue;
        }
        ++i;
        if ((c & 0xC0U) != 0x80U)
            ++columns;
    }
    return columns;
}

/// 按**可见列数**截断：绝不切开发送序列或 UTF-8 字符；真的截断了就补一个复位，
/// 避免颜色渗到下一行。按字节截断会把带颜色的进度条砍成半个 CSI，终端随后吞掉整行。
std::string truncateVisible(const std::string& text, std::size_t max_columns) {
    std::string out;
    out.reserve(text.size());
    std::size_t columns = 0;
    bool truncated = false;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
            std::size_t end = i + 2;
            while (end < text.size() && !(text[end] >= 0x40 && text[end] <= 0x7e))
                ++end;
            if (end >= text.size()) {
                truncated = true; // 残缺序列：整段丢弃
                break;
            }
            out.append(text, i, end - i + 1);
            i = end + 1;
            continue;
        }
        if ((c & 0xC0U) == 0x80U) {
            out.push_back(text[i++]); // 续字节跟随其首字节
            continue;
        }
        if (columns == max_columns) {
            truncated = true;
            break;
        }
        out.push_back(text[i++]);
        ++columns;
    }
    if (truncated && out.find('\x1b') != std::string::npos)
        out += kReset;
    return out;
}

/// 24bit 前景色；color=false 时返回空串（非交互 / NO_COLOR / dumb 终端完全不输出转义）。
std::string fg(bool color, int r, int g, int b, bool bold = false) {
    if (!color)
        return {};
    return std::string(bold ? "\x1b[1;38;2;" : "\x1b[38;2;") + std::to_string(r) + ";" + std::to_string(g) + ";" +
           std::to_string(b) + "m";
}

/// 颜色复位；color=false 时返回空串（PLAIN/OFF 一个转义都不能有）。
std::string reset(bool color) {
    return color ? std::string(kReset) : std::string();
}

/// 行速率：1.2M/s / 12.3k/s / 870/s。
std::string formatRate(double rate) {
    if (rate < 0)
        rate = 0;
    std::ostringstream oss;
    if (rate >= 1e6) {
        oss.setf(std::ios::fixed);
        oss.precision(1);
        oss << rate / 1e6 << "M/s";
    } else if (rate >= 1e3) {
        oss.setf(std::ios::fixed);
        oss.precision(1);
        oss << rate / 1e3 << "k/s";
    } else {
        oss.setf(std::ios::fixed);
        oss.precision(0);
        oss << rate << "/s";
    }
    return oss.str();
}

/// 进度条：已填充段是紫色渐变 '='，边界是 '==>' 箭头，未填充区有一段缓慢往返的暗色流光。
/// 颜色关闭时字符结构与参考实现的 ASCII 版一致（`=====>`），不会输出任何转义。
std::string renderBar(int percent, std::size_t anim, std::size_t width, bool color) {
    const int clamped = std::clamp(percent, 0, 100);
    const int filled = static_cast<int>(width * static_cast<std::size_t>(clamped) / 100);

    std::vector<int> cells(width, 0); // 0 = 空，1 = 已填充，2 = 箭头，3 = 流光
    for (int i = 0; i < filled; ++i)
        cells[static_cast<std::size_t>(i)] = 1;

    const int remaining = static_cast<int>(width) - filled;
    int arrow_len = 0;
    if (clamped < 100 && remaining > 0) {
        arrow_len = std::min(remaining, 3);
        for (int i = 0; i < arrow_len; ++i)
            cells[static_cast<std::size_t>(filled + i)] = 2;
    }

    // 流光只在箭头之后的空白区往返；空间不够就不画，避免和箭头重叠
    const int shimmer_start = filled + arrow_len;
    constexpr int kShimmerLen = 3;
    if (clamped < 100 && static_cast<int>(width) - shimmer_start > kShimmerLen) {
        const int span = static_cast<int>(width) - shimmer_start - kShimmerLen;
        const int cycle = static_cast<int>(anim % static_cast<std::size_t>(2 * span));
        const int pos = cycle <= span ? cycle : 2 * span - cycle;
        for (int i = 0; i < kShimmerLen; ++i)
            cells[static_cast<std::size_t>(shimmer_start + pos + i)] = 3;
    }

    std::string out = color ? std::string(kFrameColor) + "[" : std::string("[");
    for (int j = 0; j < static_cast<int>(width); ++j) {
        const auto index = static_cast<std::size_t>(j);
        if (cells[index] == 2) {
            // 箭头整段输出一次（可能只有 1~3 格）
            if (arrow_len >= 3) {
                out += fg(color, 255, 105, 255, /*bold=*/true) + "==";
                out += fg(color, 255, 60, 60, /*bold=*/true) + ">";
                j += 2;
            } else {
                out += fg(color, 255, 60, 60, /*bold=*/true) + ">";
            }
            if (color)
                out += kReset;
            continue;
        }
        if (cells[index] == 1) {
            const int r = 80 + j * 175 / std::max<int>(1, static_cast<int>(width));
            out += fg(color, r, 0, 255) + "=";
            continue;
        }
        if (cells[index] == 3) {
            out += fg(color, 95, 75, 130) + "·";
            continue;
        }
        out.push_back(' ');
    }
    if (color)
        out += kReset;
    out += "]";
    return out;
}

int percentOf(int64_t done, int64_t total) {
    if (total <= 0)
        return 100;
    return static_cast<int>(std::min<int64_t>(100, done * 100 / total));
}

std::string formatSeconds(std::chrono::steady_clock::duration elapsed) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(ms < 10000 ? 2 : 1);
    oss << static_cast<double>(ms) / 1000.0 << "s";
    return oss.str();
}

} // namespace

std::vector<std::string> formatPlanLines(const LoadPlan& plan) {
    int64_t node_rows = 0;
    int64_t edge_rows = 0;
    for (const auto& item : plan.nodes)
        node_rows += item.rows;
    for (const auto& item : plan.edges)
        edge_rows += item.rows;

    std::vector<std::string> lines;
    lines.push_back("[loader] Load plan: graph '" + plan.graph + "', " + std::to_string(plan.nodes.size()) +
                    " node label(s) / " + std::to_string(plan.edges.size()) + " edge type(s), " +
                    formatCount(node_rows) + " / " + formatCount(edge_rows) + " rows");

    auto item_line = [](const char* kind, const PlanItem& item) {
        std::string line = std::string("[loader]   ") + kind + "  " + padRight(item.name, 16) + " " +
                           padLeft(std::to_string(item.files) + (item.files == 1 ? " file" : " files"), 8) + " " +
                           padLeft(formatCount(item.rows) + " rows", 14);
        if (!item.detail.empty())
            line += "  (" + item.detail + ")";
        return line;
    };
    for (const auto& item : plan.nodes)
        lines.push_back(item_line("nodes", item));
    for (const auto& item : plan.edges)
        lines.push_back(item_line("edges", item));
    return lines;
}

ProgressReporter::ProgressReporter(ProgressMode mode, std::ostream& out, bool force_interactive) : out_(out) {
    enabled_ = mode != ProgressMode::OFF;
    interactive_ = enabled_ && mode == ProgressMode::AUTO &&
                   (force_interactive || (&out == &std::cerr && ::isatty(::fileno(stderr)) != 0));
    animate_ = interactive_ || (enabled_ && mode == ProgressMode::PLAIN);
    const char* no_color = std::getenv("NO_COLOR");
    const char* term = std::getenv("TERM");
    color_ = interactive_ && no_color == nullptr && (term == nullptr || std::strcmp(term, "dumb") != 0);
    last_plain_ = std::chrono::steady_clock::now();
    if (animate_)
        ticker_ = std::thread([this] { tickerLoop(); });
}

ProgressReporter::~ProgressReporter() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    ticker_cv_.notify_all();
    if (ticker_.joinable())
        ticker_.join();

    std::lock_guard<std::mutex> lock(mu_);
    eraseBlockLocked();
    if (cursor_hidden_) {
        out_ << kShowCursor;
        cursor_hidden_ = false;
    }
    for (const auto& line : permanent_)
        out_ << line << '\n';
    permanent_.clear();
    out_.flush();
}

void ProgressReporter::printPlan(const LoadPlan& plan) {
    if (!enabled_)
        return;
    const auto lines = formatPlanLines(plan);
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& line : lines)
        out_ << truncateVisible(line, kMaxLineColumns) << '\n';
    out_.flush();
}

void ProgressReporter::beginPhase(const std::string& phase, const std::string& item_noun,
                                  const std::vector<PlanItem>& items) {
    std::lock_guard<std::mutex> lock(mu_);
    phase_ = phase;
    item_noun_ = item_noun;
    items_.clear();
    item_index_.clear();
    active_.clear();
    completed_.clear();
    permanent_.clear();
    phase_done_ = 0;
    phase_total_ = 0;
    items_done_ = 0;
    anim_ = 0;
    phase_active_ = true;
    phase_start_ = std::chrono::steady_clock::now();
    phase_last_sample_ = phase_start_;
    phase_last_rows_ = 0;
    phase_speed_ = 0.0;
    last_plain_ = phase_start_;

    for (const auto& item : items) {
        item_index_[item.name] = items_.size();
        items_.push_back(ItemState{item.name, item.rows, item.files, 0, 0});
        phase_total_ += item.rows;
    }

    if (enabled_) {
        out_ << fg(color_, 200, 100, 255, /*bold=*/true) << "[loader] Loading " << phase_ << ":" << reset(color_) << " "
             << items_.size() << " " << item_noun_ << ", " << formatCount(phase_total_) << " rows\n";
        out_.flush();
    }
    if (interactive_)
        renderLocked();
}

void ProgressReporter::beginFile(const std::string& item, const std::string& file, int64_t rows) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    FileState state;
    state.item = item;
    state.file = file;
    state.total_rows = rows;
    state.started = now;
    active_.push_back(std::move(state));

    if (!interactive_) {
        if (!enabled_)
            return;
        out_ << "  -> " << item << "  " << file << "  (" << formatCount(rows) << " rows)\n";
        out_.flush();
        return;
    }
    renderLocked();
}

void ProgressReporter::addRows(const std::string& item, const std::string& file, int64_t rows) {
    if (rows <= 0)
        return;
    std::lock_guard<std::mutex> lock(mu_);
    if (auto it = item_index_.find(item); it != item_index_.end()) {
        items_[it->second].done_rows += rows;
        phase_done_ += rows;
    }
    for (auto& active : active_) {
        if (active.item == item && active.file == file) {
            active.done_rows += rows;
            break;
        }
    }
}

void ProgressReporter::endFile(const std::string& item, const std::string& file, int64_t written, int64_t skipped) {
    std::lock_guard<std::mutex> lock(mu_);
    auto active_it = std::find_if(active_.begin(), active_.end(),
                                  [&](const FileState& s) { return s.item == item && s.file == file; });
    if (active_it == active_.end())
        return;
    const FileState finished = *active_it;
    active_.erase(active_it);

    if (auto it = item_index_.find(item); it != item_index_.end()) {
        auto& state = items_[it->second];
        state.done_files += 1;
        if (state.done_files >= state.total_files)
            ++items_done_;
    }

    std::string line = "  ok " + padRight(item, 8) + " " + finished.file + "  " + formatCount(finished.done_rows) +
                       " rows (written " + formatCount(written);
    if (skipped > 0)
        line += ", skipped " + formatCount(skipped);
    line += ")  " + formatSeconds(std::chrono::steady_clock::now() - finished.started);

    if (!interactive_) {
        if (!enabled_)
            return;
        out_ << truncateVisible(line, kMaxLineColumns) << '\n';
        out_.flush();
        return;
    }
    completed_.push_back(truncateVisible(line, kMaxLineColumns));
    if (completed_.size() > kMaxBlockLines) {
        permanent_.push_back(completed_.front());
        completed_.erase(completed_.begin());
    }
    renderLocked();
}

void ProgressReporter::endPhase() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!phase_active_)
        return;
    phase_active_ = false;
    if (enabled_) {
        eraseBlockLocked();
        if (cursor_hidden_) {
            out_ << kShowCursor;
            cursor_hidden_ = false;
        }
        for (const auto& line : permanent_)
            out_ << line << '\n';
        permanent_.clear();
        const auto elapsed = std::chrono::steady_clock::now() - phase_start_;
        const double seconds = std::max(1e-9, std::chrono::duration<double>(elapsed).count());
        out_ << fg(color_, 200, 100, 255, /*bold=*/true) << "[loader] " << phase_ << " done:" << reset(color_) << " "
             << formatCount(phase_done_) << " rows, " << items_.size() << " " << item_noun_ << " in "
             << formatSeconds(elapsed) << " (" << formatRate(static_cast<double>(phase_done_) / seconds) << ")\n";
        out_.flush();
    }
    items_.clear();
    item_index_.clear();
    active_.clear();
    completed_.clear();
}

void ProgressReporter::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    infoLocked(message, /*is_warning=*/false);
}

void ProgressReporter::warn(const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    infoLocked(message, /*is_warning=*/true);
}

void ProgressReporter::infoLocked(const std::string& message, bool is_warning) {
    if (!interactive_) {
        if (is_warning)
            spdlog::warn("{}", message);
        else
            spdlog::info("{}", message);
        return;
    }
    eraseBlockLocked();
    if (is_warning)
        spdlog::warn("{}", message);
    else
        spdlog::info("{}", message);
    renderLocked();
}

int64_t ProgressReporter::phaseRowsDone() const {
    std::lock_guard<std::mutex> lock(mu_);
    return phase_done_;
}

int64_t ProgressReporter::phaseRowsTotal() const {
    std::lock_guard<std::mutex> lock(mu_);
    return phase_total_;
}

int64_t ProgressReporter::itemRowsDone(const std::string& item) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (auto it = item_index_.find(item); it != item_index_.end())
        return items_[it->second].done_rows;
    return 0;
}

void ProgressReporter::tickerLoop() {
    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        ticker_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stop_; });
        if (stop_)
            break;
        if (!phase_active_)
            continue;
        ++anim_;
        if (interactive_) {
            sampleSpeedsLocked();
            renderLocked();
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_plain_ >= std::chrono::seconds(5)) {
            last_plain_ = now;
            out_ << truncateVisible(overallLineLocked(), kMaxLineColumns) << '\n';
            out_.flush();
        }
    }
}

void ProgressReporter::sampleSpeedsLocked() {
    const auto now = std::chrono::steady_clock::now();
    const auto sample = [&now](int64_t done, int64_t& last_rows, std::chrono::steady_clock::time_point& last_time,
                               double& speed) {
        const double seconds = std::chrono::duration<double>(now - last_time).count();
        if (seconds < 0.5)
            return;
        speed = static_cast<double>(done - last_rows) / seconds;
        last_rows = done;
        last_time = now;
    };
    sample(phase_done_, phase_last_rows_, phase_last_sample_, phase_speed_);
}

std::string ProgressReporter::overallLineLocked() const {
    const int percent = percentOf(phase_done_, phase_total_);
    const auto elapsed = std::chrono::steady_clock::now() - phase_start_;
    const double seconds = std::max(1e-9, std::chrono::duration<double>(elapsed).count());
    const double avg = static_cast<double>(phase_done_) / seconds;

    std::string line = fg(color_, 200, 100, 255, /*bold=*/true) + "[" + phase_ + "]" + reset(color_);
    line += " " + fg(color_, 150, 150, 150) + std::to_string(items_done_) + "/" + std::to_string(items_.size()) + " " +
            item_noun_ + reset(color_);
    line += " " + padLeft(std::to_string(percent) + "%", 4) + " ";
    line += renderBar(percent, anim_, kOverallBarWidth, color_);
    line += " " + fg(color_, 220, 220, 220) + formatCount(phase_done_) + "/" + formatCount(phase_total_) + " rows" +
            reset(color_);
    line += " " + fg(color_, 150, 150, 150) + formatSeconds(elapsed) + "  avg " + formatRate(avg);
    // 实时速度：短于 0.5s 时采样还没有意义，此时只显示均值
    if (seconds >= 0.5 && phase_speed_ > 0)
        line += "  now " + formatRate(phase_speed_);
    line += reset(color_);
    return line;
}

std::vector<std::string> ProgressReporter::composeLocked() const {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> lines;
    lines.push_back(truncateVisible(overallLineLocked(), kMaxLineColumns));
    for (const auto& line : completed_)
        lines.push_back(line);
    for (const auto& active : active_) {
        const int percent = percentOf(active.done_rows, active.total_rows);
        std::string line = "  " + fg(color_, 200, 100, 255) + "··" + reset(color_) + " " + fg(color_, 200, 200, 200) +
                           padRight(active.item, 8) + reset(color_) + " " + fg(color_, 150, 150, 150) +
                           truncateUtf8(active.file, 40) + reset(color_) + " " +
                           padLeft(std::to_string(percent) + "%", 4) + " " +
                           renderBar(percent, anim_, kFileBarWidth, color_) + " " + fg(color_, 220, 220, 220) +
                           formatCount(active.done_rows) + "/" + formatCount(active.total_rows) + reset(color_) + " " +
                           fg(color_, 150, 150, 150) + formatSeconds(now - active.started) + "  " +
                           formatRate(static_cast<double>(active.done_rows) /
                                      std::max(1e-9, std::chrono::duration<double>(now - active.started).count())) +
                           reset(color_);
        lines.push_back(truncateVisible(line, kMaxLineColumns));
    }
    return lines;
}

void ProgressReporter::renderLocked() {
    if (!interactive_ || !phase_active_)
        return;
    eraseBlockLocked();
    if (!cursor_hidden_) {
        out_ << kHideCursor;
        cursor_hidden_ = true;
    }
    for (const auto& line : permanent_)
        out_ << line << '\n';
    permanent_.clear();
    auto lines = composeLocked();
    for (const auto& line : lines)
        out_ << line << '\n';
    out_.flush();
    rendered_lines_ = lines.size();
}

void ProgressReporter::eraseBlockLocked() {
    if (!interactive_ || rendered_lines_ == 0)
        return;
    // 上移到块首行行首并清屏到末尾：块内所有行都是我们画的，直接整体重绘
    out_ << "\x1b[" << rendered_lines_ << "F\x1b[J";
    out_.flush();
    rendered_lines_ = 0;
}

} // namespace loader
} // namespace eugraph
