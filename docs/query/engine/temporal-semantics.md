# 时间类型的语义（对齐 neo4j）— 设计与验证

> 分支: `fix/type-op-bugs`
> 状态: **阶段 1 已实现并对照验证**（50/50 与 neo4j 5.26.30 一致）

## 背景

另一款图数据库的《测试报告_类型与运算_BUG记录.md》列了 16 个类型/运算问题（不代表我们也有）。本分支的做法是**逐条用本机 neo4j 5.26.30 对照**：同一条语句在 neo4j（7687）与我们（7688）各跑一次，以 neo4j 的输出为判定基准。对照脚本见下方「验证方式」。

分类结果：

| 类别 | 项 |
|------|-----|
| 我们确实存在的语义问题 | BUG-01/02/03/04/05/06（时间语义，本文件）、BUG-10/11（参数与序列化）、BUG-12/13（错误模型）、BUG-15/16（函数） |
| 我们没有的问题 | BUG-08 `keys(map)` 顺序稳定、BUG-09 `percentileDisc` 保持整数、BUG-14 `ORDER BY 1` 正常 |
| 不做 | BUG-07 `point()` / CRS（开发者决定不实现） |

## 已对齐的规则（每条都有 neo4j 实测值）

### 1. `date ± duration`：按月加减时对"日"做夹取

`2024-03-31 - P1M` 必须得到 `2024-02-29`（旧实现先加月再规范化日期，溢出成 `2024-03-02`）。规则：先按 `months` 调整年月，把日夹取到目标月的最后一天，再叠加 `days`/`seconds`。

| 表达式 | neo4j |
|--------|-------|
| `date('2024-03-31') - duration('P1M')` | `2024-02-29` |
| `date('2024-05-31') - duration('P1M')` | `2024-04-30` |
| `date('2024-03-31') - duration('P1M1D')` | `2024-02-28` |
| `date('2024-01-31') + duration('P1M')` | `2024-02-29` |

### 2. `duration.between(from, to)`：java.time 单元的组合

neo4j 的实现（`community/values/.../DurationValue.java#durationBetween`）是四个 java.time 单元的串联，我们按同样顺序实现：

```
months = ChronoUnit.MONTHS.between(from, to)   // 月的"打包"比较（月*32+日）；带时刻的值若 end 时刻更早，
                                               // 先把 end 的日期减一天
from  += months                                // plusMonths：日夹取（同第 1 条）
days   = ChronoUnit.DAYS.between(from, to)     // 纯 EPOCH_DAY 差，不看时刻
nanos  = ChronoUnit.NANOS.between(from, to)    // 精确剩余，可为负
```

两个容易踩的点：

- **反向不是正向取负**：`duration.between(Mar31, Feb29)` 是 `P-1M`，而正向 `duration.between(Feb29, Mar31)` 是 `P1M2D`。这是 Java 截断除法 + `plusMonths` 夹取的直接结果，不能靠"取负"省事。
- **日/时间的借位只发生在 `between` 内部**：剩余时间为负时，`between` 会从 `days` 借一天（`30 天 - 1 小时` → `29 天 + 23 小时`）。构造出来的 duration **不折**：`duration({days: 1, seconds: -3600})` 保持原样，`toString` 输出 `P1DT-1H`（与 neo4j 一致）。

| 表达式 | neo4j |
|--------|-------|
| `duration.between(date('2024-01-31'), date('2024-03-01'))` | `P1M1D` |
| `duration.between(date('2024-01-31'), date('2024-02-29'))` | `P29D` |
| `duration.between(date('2024-03-31'), date('2024-02-29'))` | `P-1M` |
| `duration.between(datetime('2024-01-31T10:00:00Z'), datetime('2024-03-01T09:00:00Z'))` | `P29DT23H` |
| `duration.between(datetime('2024-01-15T10:00:00Z'), datetime('2024-02-29T09:00:00Z'))` | `P1M13DT23H` |

### 3. `date - date`：保留我们的扩展，但必须自洽（刻意偏离 neo4j）

neo4j 对 `date - date` 直接报 `Type mismatch: expected Duration but was Date`；**我们继续支持它**（不因为参考实现不支持就砍掉已有能力）。但它的语义必须与 `duration.between` 一致，否则同一次减法会有两种答案：

```
date('2024-03-01') - date('2024-01-31')      == duration.between(date('2024-01-31'), date('2024-03-01'))   // 都是 P1M1D
```

即 `subtractDateTimes(a, b) = durationBetween(b, a)`。这条偏离已由开发者确认。

### 4. 带时区的 `datetime` / `time` 比较：时刻优先，同刻按本地时间

neo4j 的规则可以概括为**元组序 `(绝对时刻, 本地时间字段)`**，`=` 还要求时区一致（6 个运算符在 4 组用例上全部自洽）：

| 表达式 | neo4j |
|--------|-------|
| `datetime('2024-01-01T00:00:00Z') = datetime('2024-01-01T08:00:00+08:00')` | `false` |
| `… < …` （同一时刻，本地 00:00 vs 08:00） | `true` |
| `… <= …` / `… > …` / `… >= …` / `… <> …` | `true` / `false` / `false` / `true` |
| `datetime('2024-01-01T00:00:00Z') >= datetime('2024-01-01T00:00:00+08:00')`（相差 8 小时） | `true` |
| `time('12:00:00Z') < time('20:00:00+08:00')`（同刻） | `true` |

旧实现里 `=` 用"本地字段+时区"、`<`/`>` 用"绝对时刻"，于是出现 `<=` 与 `>=` 同时为真、`<>` 也为真的自相矛盾组合。

### 5. `toString()` 始终输出秒

零秒不省略：`toString(localdatetime('2024-06-15T12:30:00'))` = `2024-06-15T12:30:00`（旧实现输出 `2024-06-15T12:30`）。纳秒仍只在非零时输出。

### 6. epoch 基准键：只有 `epochSeconds` 与 `epochMillis`

`epochSeconds` 曾完全没被当作基准，于是 `datetime({epochSeconds: 0, timezone:'+08:00'})` 落到字段默认值：本地 `1970-01-01T00:00+08:00`，`.epochSeconds` 变成 `-28800`。现在 epoch 是**绝对时刻**，`timezone` 只决定本地字段的展示时区（`.epochSeconds` 恒为 `0`）。

`epochMicros` / `epochNanos` 在 neo4j 里不是合法字段（报 `No such field`），因此不实现。

**已处理（错误模型阶段）**：未知 map 键现在报 `No such field: <key>`（duration 用 `Unknown field: <key>`），
`date({year:'2024'})` 这类类型错误报 `year must be an integer value, but was a UTF8StringValue`，
越界取值（年/月/日/时/分/秒/亚秒/周/序日/季度）报 `Invalid value for <Field> (valid values ...)`
—— 详见 [error-model.md](error-model.md)。

### 7. `toString()` 保留命名时区后缀（刻意偏离 neo4j）

`toString(datetime('2024-01-01T12:00:00[UTC]'))` 我们输出 `2024-01-01T12:00:00+00:00[UTC]`，
neo4j 只输出偏移（`2024-01-01T12:00:00+00:00`）。保留 `[Zone]` 让 `datetime(toString(x))` 不丢时区名，
属于既有能力，本阶段不动；只在错误分类上收紧（`time('12:00:00[UTC]')` 已按 neo4j 报 ArgumentError）。

### 8. `toString()` 带秒，客户端/TCK 文本渲染不带零秒

同一个值有两种文本渲染，各有依据，不要互相“对齐”：

* `toString(v)`（服务端语义，neo4j 为准）：秒始终输出 —— `toString(localdatetime('2024-06-15T12:30:00'))` = `2024-06-15T12:30:00`；
* Thrift / Shell / TCK 的**值文本**（`temporalToIsoString`）：按 ISO-8601 省略零秒与零小数 —— `2024-06-15T12:30`，
  这与各语言驱动渲染 Bolt 时间值的形式一致（neo4j + cypher-shell 打印 `1816-01-01T00:00`、`12:34Z`），
  openCypher TCK 的期望文本也用的是这个约定。

把两者混为一谈会让 TCK 的字符串比较一次性失败约 410 个场景（曾实际发生过），而“修 TCK”就会破坏 BUG-04 要求的 `toString`。

**负亚秒已归一化**：Java `Duration` 的不变量是 `0 ≤ nanos < 1e9`，符号由 `seconds` 承担；
我们以前在构造处维持的是"seconds 与 nanos 同号"的另一种约定，于是 `.seconds` / `.nanosecondsOfSecond`
和 neo4j 不一致（`duration({nanoseconds:-900000000}).seconds` 曾给 `0`、neo4j 给 `-1`），
`duration.between` 的负余量同理（TCK `Temporal10` 曾因此失败 8 个场景）。
现在 `normalizeDurationNanos()` 在所有构造点（map/字符串/duration.between/加减乘除/Bolt 参数）统一收口，
文本形式不变（`PT-0.9S`），访问器与 neo4j 一致，TCK `Temporal10` 全绿。

### 9. 历史命名时区偏移取决于 tzdata 版本

`datetime('1818-07-21T21:40:32.142[Europe/Stockholm]')` 的偏移，neo4j 给 `+00:53:28`
（它自带一份 tzdata），我们用平台的 C++20 时区库（系统 tzdata）给 `+01:12:12`。
两者都是各自数据库里的 Stockholm LMT，属于**环境差异**而不是换算逻辑错误：
1879 年以前的 LMT 在不同 tzdata 版本间被修订过。TCK 的 `Temporal2 [6] ex #5` 因此仍失败 1 个场景。

### 10. 极值年份：字段可以窄，算术必须宽

`year` 是 `int32`（合法范围 ±999'999'999）、`month/day/hour/minute/second` 是 `int8`、`nanos` 是 `int32`
—— 这只约束**存储**，不约束中间量。凡是拿字段做算术的地方都必须先拓宽，否则 UBSan 直接报
`signed integer overflow` 并打挂 server（CI 的 ubsan 作业就是按这个判据抓的，TCK `Temporal10 [9]` 曾因此崩在
`duration.between` 上）：

| 场景 | 中间量 | 收口位置 |
|---|---|---|
| 年月算术（`year*12 + month`、java.time 的"月+日"打包坐标） | `int64` | `absoluteMonths()` / `packedMonthDay()`（`temporal_value.hpp`）；不要再手写 `year * 12` |
| 日 → 纳秒、两个时刻相减（`between` 与 `inSeconds` 的带时区分支） | `__int128` | `localFieldsNanos()`、`durationInSecondsScalarFn` 的 `utcNanos` |
| epoch 秒 → 纳秒（`datetime.fromepoch`、Bolt 参数解码） | `__int128` | `datetimeFromEpoch()`；结果年份超出 ±999'999'999 时报 `ArgumentError`，不静默回绕 |

判据（都在 `tests/test_query_executor.cpp` 的 `Temporal*` 里）：

* `duration.between(date('-999999999-01-01'), date('+999999999-12-31'))` = `P1999999998Y11M30D`（TCK `Temporal10 [9]`）
* `duration.inSeconds(localdatetime('-999999999-01-01'), localdatetime('+999999999-12-31T23:59:59'))` = `PT17531639991215H59M59S`（`[10]`）
* `datetime.fromepoch(100000000000, 0)` = `5138-11-16T09:46:40Z`（`|seconds| > 9.2e9` 即超出 int64 的纳秒容量）

**已知缺口（未处理）**：日期/时间的**渲染**是按位取数（`pad4`/`pad2`），只支持 0–9999 的四位年份。
负年份会打印出非数字字符，年份 ≥ 10000 会被截成低四位 —— 值本身是对的：

| 表达式 | 值（正确） | 当前打印（错） |
|---|---|---|
| `datetime.fromepoch(-100000000000, 0)` | `year = -1199` | `//''-02-15T14:13:20Z` |
| `datetime.fromepoch(300000000000, 0)` | `year = 11476` | `1476-08-15T05:20:00Z` |

TCK 目前只用极值年份做 duration 的输入，所以还没暴露；修它要同时对齐 neo4j 的文本形式
（`-0001-01-01`、`10000-01-01`）。

## 验证方式

开发期把用例同时打到 neo4j 5.26（7687）与 eugraph（7688）逐条比对，输出一致才写入断言；
对照脚本属一次性工具，不入库。回归固定在 `tests/test_query_executor.cpp` 的 `Temporal*` 用例里。

回归固定在 `tests/test_query_executor.cpp` 的 `Temporal*` 用例里，期望值即上表的 neo4j 实测值。
