#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace loader {

/// 进度显示方式。
enum class ProgressMode {
    AUTO,  ///< TTY 上画原地刷新的进度块；输出被重定向（非 TTY）时自动退化为 PLAIN
    PLAIN, ///< 只打普通文本行：每个文件开始/结束 + 周期性总进度
    OFF,   ///< 不输出动态进度；计划表仍由 printPlan 打印（它说明「装什么」，不是动画）
};

/// 装载计划里的一个条目：一个顶点标签或一个边类型。
struct PlanItem {
    std::string name; ///< 标签名 / 边类型名
    std::size_t files = 0;
    int64_t rows = 0;   ///< 预扫描得到的记录数（不含表头）
    std::string detail; ///< 边类型：`src -> dst`；顶点留空
};

/// 本次装载计划：先告诉使用者「要装哪些标签、哪些边」。
struct LoadPlan {
    std::string graph;
    std::vector<PlanItem> nodes;
    std::vector<PlanItem> edges;
};

/// 计划表的纯文本行（无 ANSI 控制符），便于测试与复用。
std::vector<std::string> formatPlanLines(const LoadPlan& plan);

/// 终端进度显示。
///
/// 布局（AUTO + TTY）：
///   [vertices] 3/8 labels  45% [#########>············] 147,000/327,588 rows   ← 总进度：始终第一行，原地刷新
///     ·· Person  person_0_0.csv  78% [#######>········] 123,400/158,000 rows   ← 正在装载：原地刷新（跑马灯）
///     ok Place   place_0_0.csv  100% [################]   1,460/1,460 rows     ← 装完定格保留
///
/// 每装完一个文件就定格该行、下一个文件另起一行；`--parallel-files>1` 时最多 N 行同时刷新。
/// 行数过多时早期完成行会被挤出成普通行（进入终端回滚区），进度块本身保持有界。
/// 非 TTY、PLAIN、OFF 都**不输出任何控制字符**。
class ProgressReporter {
public:
    /// force_interactive 仅供测试：跳过 isatty 判定，便于在单测里检查原地刷新块的内容
    /// （生产路径永远由真实 TTY 决定）。
    explicit ProgressReporter(ProgressMode mode, std::ostream& out, bool force_interactive = false);
    ~ProgressReporter();

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;

    bool interactive() const {
        return interactive_;
    }

    /// 打印装载计划（每个标签/边类型一行）。OFF 模式同样打印。
    void printPlan(const LoadPlan& plan);

    /// 开始一个阶段（vertices / edges）。items 决定总数与分母。
    void beginPhase(const std::string& phase, const std::string& item_noun, const std::vector<PlanItem>& items);

    void beginFile(const std::string& item, const std::string& file, int64_t rows);
    void addRows(const std::string& item, const std::string& file, int64_t rows);
    void endFile(const std::string& item, const std::string& file, int64_t written, int64_t skipped);
    void endPhase();

    /// 立即重绘一次（交互模式；PLAIN 模式立即打一行状态，OFF 无操作）。
    /// 供调用方在推进了一大批数据后主动刷新，也让测试不必依赖 ticker 的时序。
    void flush();

    /// 装载途中打印一行日志：先擦掉进度块、打印后重绘，避免日志与动画交错。
    void info(const std::string& message);
    void warn(const std::string& message);

    // 只读状态（测试与摘要用）
    int64_t phaseRowsDone() const;
    int64_t phaseRowsTotal() const;
    int64_t itemRowsDone(const std::string& item) const;

private:
    struct ItemState {
        std::string name;
        int64_t total_rows = 0;
        std::size_t total_files = 0;
        int64_t done_rows = 0;
        std::size_t done_files = 0;
    };
    struct FileState {
        std::string item;
        std::string file;
        int64_t total_rows = 0;
        int64_t done_rows = 0;
        std::chrono::steady_clock::time_point started{};
    };

    void tickerLoop();
    void sampleSpeedsLocked();
    void renderLocked();
    void eraseBlockLocked();
    void infoLocked(const std::string& message, bool is_warning);
    std::vector<std::string> composeLocked() const;
    std::string overallLineLocked() const;

    std::ostream& out_;
    bool enabled_ = false; ///< OFF 模式：完全静默（计划表也不打印）
    bool interactive_ = false;
    bool animate_ = false; ///< 是否需要 ticker（交互动画或 PLAIN 周期行）

    mutable std::mutex mu_;
    std::thread ticker_;
    std::condition_variable ticker_cv_;
    bool stop_ = false;

    std::string phase_;
    std::string item_noun_;
    bool phase_active_ = false;
    int64_t phase_done_ = 0;
    int64_t phase_total_ = 0;
    std::size_t items_done_ = 0;
    std::vector<ItemState> items_;
    std::unordered_map<std::string, std::size_t> item_index_;
    std::vector<FileState> active_;
    std::vector<std::string> completed_; ///< 仍留在进度块里的完成行（最近若干条）
    std::vector<std::string> permanent_; ///< 被挤出进度块、下次重绘前永久打印的行
    std::size_t rendered_lines_ = 0;
    std::size_t anim_ = 0;
    bool color_ = false; ///< 交互模式且非 NO_COLOR/dumb 终端时输出 24bit 颜色
    bool cursor_hidden_ = false;
    std::chrono::steady_clock::time_point phase_start_{};
    std::chrono::steady_clock::time_point last_plain_{};
    std::chrono::steady_clock::time_point phase_last_sample_{};
    int64_t phase_last_rows_ = 0;
    double phase_speed_ = 0.0;
};

} // namespace loader
} // namespace eugraph
