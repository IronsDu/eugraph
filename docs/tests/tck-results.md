# TCK 测试结果分类报告

**日期**: 2026-05-31 (更新)
**分支**: fix/tck-remaining-defects
**总计**: 3897 场景, 16006 步骤
**运行耗时**: ~11 分
**检测方式**: Parser AST 遍历 + Binder 类型检查 + 运行时断言

### 时间维度 TCK 专项

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| Temporal 场景通过 | 543/1004 (54.1%) | 582/1004 (58.0%) |
| Temporal 步骤通过 | 3128/4086 (76.6%) | 3206/4086 (78.5%) |

---

## 总体结果

| 状态 | 数量 | 说明 |
|------|------|------|
| 步骤通过 | 11833 / 16006 | 73.9% |
| 步骤跳过 | 1832 | 前置步骤失败 |
| 未定义步骤 | 71 | 缺少 step 定义 |
| 步骤失败 | 2270 | 断言失败或查询错误 |
| 场景通过 | ~1556 | ~39.9% |

---

## 分类 1: 时间日期构造函数 — ✅ Phase 2 已实现

实现了 6 个构造函数族（`datetime`, `date`, `time`, `localtime`, `localdatetime`, `duration`），支持 MAP<STRING, ANY>、STRING 和 0-arg 调用。

### 构造函数

| 函数 | 状态 |
|------|------|
| `datetime()` / `date()` / `time()` | ✅ 已实现（MAP + STRING + 0-arg） |
| `localtime()` / `localdatetime()` | ✅ 已实现（MAP + STRING + 0-arg） |
| `duration()` | ✅ 已实现（MAP + STRING + 0-arg，含分数值） |

### 成员访问器

| 功能 | 状态 |
|------|------|
| `.year` / `.month` / `.day` 等 Date 字段 | ✅ 已实现（Binder 阶段枚举化） |
| `.hour` / `.minute` / `.second` 等 Time 字段 | ✅ 已实现 |
| `.timezone` / `.offset` 等时区字段 | ✅ 已实现 |
| Duration 字段 (`.years`, `.months`, `.days` 等) | ✅ 已实现 |

### 比较与算术

| 功能 | 状态 |
|------|------|
| 相等比较 (`=`, `<>`) | ✅ 已实现（泛型 Value::operator==，同 kind） |
| 有序比较 (`<`, `>`, `<=`, `>=`) | ✅ 已实现（逐字段比较避免溢出） |
| 算术运算 (`+`, `-`) | ✅ 已实现（temporal±duration, temporal-temporal, duration±duration） |
| 乘除运算 (`*`, `/`) | ✅ 已实现（duration ×/÷ number） |
| ORDER BY 排序 | ✅ 已实现（sort_physical_op 支持 DateTimeValue/TimeValue/DurationValue） |

### Phase 3 新增

| 功能 | 状态 | TCK 通过率 |
|------|------|-----------|
| `truncate()` | ✅ 已实现（全部 15 种精度 + fields map overlay + dayOfWeek/timezone 字段） | 263/322 (81.7%) |
| `duration.between()` | ✅ 已实现（日历感知月份计算 + days→seconds 折叠 + 跨类型 DATETIME/TIME 重载） | 11/131 (8.4%) |
| `duration.inMonths()` | ✅ 已实现（含跨类型重载） | — |
| `duration.inSeconds()` / `duration.inDays()` | ✅ 已实现（含跨类型重载） | — |
| `datetime.fromepoch()` / `datetime.fromepochmillis()` | ✅ 已实现 | — |
| No-arg 事务/语句/实时钟函数 | ✅ 已实现（date/localtime/time/localdatetime/datetime × transaction/statement/realtime） | — |
| 跨类型构造函数（如 `date(datetime_value)`） | ✅ 已实现 | — |
| 截断函数跨类型输入（如 `time.truncate('day', localdatetime(...))`） | ✅ 已实现 | — |
| 截断 fields map 精度保留 | ✅ 已修复（nanosecond/microsecond/millisecond 字段覆盖不再清零高位） | — |
| Datetime 格式化优化 | ✅ 秒/纳秒为 0 时省略 `:SS` | — |
| toString 支持 DateTimeValue/TimeValue/DurationValue | ✅ 已实现 | — |

### 已知限制

#### 1. ~~PropertyValue 不支持 TemporalValue~~ — ✅ 已修复

TemporalValue 已拆分为三种独立类型（`DateTimeValue`, `TimeValue`, `DurationValue`），作为 `PropertyValue` variant 的成员，`ValueCodec` 和 Thrift 层均已更新。属性存储后类型信息完整保留。

#### 2. ~~STRING 解析紧凑格式~~ — ✅ 已修复

`parseDateFromString()` / `parseTimeStr()` 已扩展支持无分隔符的紧凑格式（YYYY, YYYYDDD, YYYY-DDD, ISO week compact, HHMMSS compact）。

#### 3. ~~DOUBLE 截断~~ — ✅ 已修复

`mulDuration()` / `divDuration()` 已新增 `double` 重载，不再截断为 `int64_t`。

#### 4. 跨 kind 比较（符合语义，非 bug）

`date < duration` 等不同时间类别的比较返回 NULL，符合 Cypher 语义。不需要修复。

#### 5. ~~`temporal - temporal` 月份分量~~ — ✅ 已修复

`subtractDateTimes()` 现已实现日历感知的月份计算，与 `duration.between()` 结果一致。

#### 6. 时区命名支持（已知限制，~30 用例）

`tz_offset_min` 仅存储数值偏移量（分钟）。命名时区如 `'Europe/Stockholm'` 无法解析为正确偏移量，需引入 IANA 时区数据库映射。

**影响范围**：Temporal3 [3][10] 等场景中约 30 个用例。

#### 7. ~~时区秒精度~~ — ✅ 已修复（commit a1597ec）

`tz_offset_min` → `tz_offset_sec`，存储精度从分钟改为秒。涉及 DateTimeValue/TimeValue 结构体、parseTzOffset、temporal accessors、格式化、temporalToComparable、temporalLess、ValueCodec 序列化、Thrift handler 转换。

#### 8. ~~时间属性存取往返~~ — ✅ 已修复

根因：Binder 对无标签节点调用 `catalog_.getAnonLabelId()` 返回 `INVALID_LABEL_ID`（0），该值进入 `label_ids_`，导致 `insertVertex` 以 label_id=0 写入失败，MATCH 无法找到无标签节点。修复：Binder 中仅当 `getAnonLabelId()` 返回非 INVALID 时才加入 `label_ids`；`CreateNodePhysicalOp` 和 `SyncGraphDataStore` 增加 INVALID_LABEL_ID 防御检查。

**影响范围**：Temporal5 全部 7 个 + Temporal4 部分约 33 个用例（共 ~40）。

---

## 分类 2: AST 精确 SKIP — 不支持的语法

约 520 个场景因查询使用了尚未实现的语法而被 AST 遍历器跳过。

### 2.1 不支持的子句类型

| 子句 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| MERGE | 80 | P1 | 合并创建，需要条件扫描+创建逻辑 |
| CALL / standalone CALL | 2 | P3 | 存储过程调用 |

### 2.2 不支持的语法模式

| 模式 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| 无上界变长展开 (`[:T*]`) | 84 | P2 | DFS 无界遍历 |
| 多标签节点 (`:A:B`) | 25 | P3 | 多标签节点创建/匹配 |
| 模式推导 (Pattern comprehension) | 15 | P3 | `[x IN ... \| ...]` |
| 关系类型交替 (`[:A\|B]`) | 3 | ~~P2~~ | 已实现，仅个别场景仍跳过 |
| Parser 限制 (无法解析) | ~250 | P3 | 十六进制/八进制字面量、嵌套 WITH 等 |

---

## 分类 3: 查询错误 — 服务端返回错误

约 1500 个场景的服务端查询返回了错误。

### 3.1 函数未找到

| 错误 | 次数 |
|------|------|
| `datetime()` 系列时间函数 | ~800 |
| 其他未注册函数 | ~50 |

### 3.2 类型错误

| 错误 | 次数 |
|------|------|
| `Logical operators require boolean operands` | ~48 |
| `TypeMismatch` / `InvalidArgumentType` | ~70 |

### 3.3 绑定错误

| 错误 | 次数 | 说明 |
|------|------|------|
| NothingToReturn（级联错误） | ~400 | 前置绑定失败导致 |
| UndefinedVariable | ~100 | 变量未定义或在当前作用域不可见 |

---

## 分类 4: 未定义步骤 (Undefined Steps)

71 个场景因缺少 Gherkin 步骤定义而标记为 undefined。

| 缺失步骤模式 | 涉及场景数 | 优先级 |
|-------------|-----------|--------|
| `And there exists a procedure ...` | ~50 | P3 |
| `Given the binary-tree-N graph` | ~19 | P3 |
| `Then a ProcedureError should be raised ...` | 2 | P3 |

全部为 CALL/procedure 相关。存储过程支持是远期目标。

---

## 分类 5: 结果不匹配 — 断言失败

约 956 个场景的查询成功执行，但返回结果与 TCK 预期不符。

主要原因：
- **NULL 传播语义**: Cypher 标准 NULL 逻辑与当前实现有差异
- **排序差异**: TCK 期望有序结果但 server 返回无序（或反之）
- **数值精度**: 浮点数序列化精度差异
- **聚合空值处理**: 全局聚合无输入时的默认行为差异

---

## 已实现特性清单

以下特性已实现，TCK 中不再跳过：

| 特性 | 说明 |
|------|------|
| AND / OR / XOR / NOT 类型检查 | Binder 阶段检查操作数类型，非布尔时报 SyntaxError: InvalidArgumentType；批量函数正确处理 NULL 传播 |
| XOR 表达式 | `a XOR b` 语法解析、类型检查、求值完整支持，Boolean TCK 通过率 86% |
| UNWIND | 列表展开为行 |
| DELETE / DETACH DELETE | 删除顶点/边，支持 DETACH 级联删除 |
| OPTIONAL MATCH | 左连接语义 |
| REMOVE | 属性/标签/边属性移除 |
| IN / XOR / CASE | 表达式运算符 |
| STARTS WITH / ENDS WITH / CONTAINS | 字符串匹配运算符 |
| EXISTS 子查询 | SemiJoin + AntiSemiJoin |
| 量词表达式 (ALL/ANY/NONE/SINGLE) | 列表量词 + 路径谓词 |
| 多 MATCH 子句 | CrossProduct 笛卡尔积 |
| 参数化查询 ($param) | Thrift RPC 传参 + Binder 替换 |
| 无源 RETURN | `RETURN 1 + 2` 等常量表达式 |
| WITH 为首子句 | `WITH 1 AS x RETURN x` |
| SET += / SET = (map) | 合并/替换属性 |
| keys(Vertex/Edge) / labels(Vertex) | 内省函数 |
| properties(Vertex/Edge) | 属性 map 函数 |
| coalesce / trim / ltrim / rtrim / split / replace / substring / left / right | 字符串函数 |
| range / toInteger / toFloat / toString / head / last / reverse / size | 标量函数 |
| UNION / UNION ALL | 查询合并（去重/不去重），列名校验，混用检测 |
| BinaryOp/UnaryOp 表达式列名 | `x > d` 等比较表达式正确输出列名（不再为 `?`） |
| 时间 truncate 'day' 精度 | `time.truncate('day', ...)` / `localtime.truncate('day', ...)` |
| durationBetween 时区修正 | `datetime` 带时区偏移时正确计算时间差 |
| week 构造函数 weekYear | `date({weekYear:..., week:..., dayOfWeek:...})` |
| 时间属性类型保留 | `CREATE` 时从 BoundExpression 推断 PropertyType（DATETIME/TIME/DURATION） |
| PropertyValue 时间数组类型 | variant 添加 `vector<DateTimeValue/TimeValue/DurationValue>` + ValueCodec 编解码 |
| Thrift 时间数组支持 | `PropertyType` 枚举新增 `DATETIME_ARRAY=11` / `TIME_ARRAY=12` / `DURATION_ARRAY=13`；`PropertyValueThrift` 支持对应数组字段 |
| 逻辑运算符 truthiness | AND/OR/XOR/NOT 接受任意类型操作数，通过 Cypher truthiness 规则转换，正确处理 null 传播 |
| 时间属性存取往返 | `CREATE` 存入时间值后 `MATCH` 读出再访问字段（如 `.year`）正确返回；无标签节点 INSERT 修复 |
| INVALID_LABEL_ID 防御 | Binder 不将 INVALID_LABEL_ID 加入 label_ids；物理算子和存储层增加防御检查 |
| 顶点序列化格式 (id + label + props) | TCK 期望格式 |

---

## 优先级汇总

| 优先级 | 分类 | 影响场景数 | 修复路径 |
|--------|------|-----------|---------|
| ~~P0~~ | ~~时间日期构造函数 & 成员访问器 & 比较/算术~~ | ~~~800~~ | ✅ 已实现（Phase 1 + Phase 2） |
| ~~P0~~ | ~~`truncate()` / `duration.between()` / STRING 解析~~ | ~~~500~~ | ✅ 已实现（Phase 3） |
| ~~P1~~ | ~~Temporal 属性往返类型保留~~ | ~~~52~~ | ✅ 已修复（拆分为 DateTimeValue/TimeValue/DurationValue） |
| ~~P2~~ | ~~时间属性存取往返~~ | ~~~40~~ | ✅ 已修复：Binder catalog_.getAnonLabelId() 返回 INVALID_LABEL_ID 导致无标签节点写失败 |
| ~~P2~~ | ~~时区秒精度~~ | ~~~48~~ | ✅ 已修复（commit a1597ec）：tz_offset_min→tz_offset_sec |
| **P1** | MERGE | ~80 | MERGE 子句实现 |
| **P2** | 无上界变长展开 | ~84 | DFS 无界遍历 |
| ~~P2~~ | ~~布尔类型检查~~ | ~~~48~~ | ✅ 已实现：AND/OR/XOR/NOT 在 Binder 阶段检查操作数类型为非布尔时报告 SyntaxError: InvalidArgumentType；批量函数正确处理 NULL 传播；XOR 从 AST skip 列表移除 |
| **P2** | 结果不匹配 | ~956 | 逐一分析（NULL 语义、排序、精度） |
| **P3** | Parser 限制 | ~250 | Parser 增强 |
| ~~P3~~ | ~~Boolean null 传播~~ | ~~~12~~ | ✅ 已修复：逻辑运算符（AND/OR/XOR/NOT）接受任意类型操作数，通过 truthiness 规则转换，null 正确传播 |
| ~~P3~~ | ~~NOT 非布尔字面量~~ | ~~~9~~ | ✅ 已修复：`NOT []` / `NOT {}` 等非布尔操作数通过 truthiness 转换后正确处理 |
| **P3** | 缺失步骤定义 | 71 | 补实现步骤（远期） |
| **P3** | 多标签节点 | 25 | 多标签创建/匹配 |

---

## 已知限制（通用）

- **数组属性存储不完整**：`CREATE ({prop: [1,2,3]})` 中列表属性未被正确写入（各类型均受影响，含时间数组）。时间数组的编解码基础设施已就位，剩余问题在 `CreateNodePhysicalOp` / `SetPhysicalOp` 的 evaluator→PropertyValue 转换管道。详见 [query-engine-design.md](../query/engine/query-engine-design.md#十二已知限制与后续规划)
- ~~**Thrift PropertyType 未扩展时间数组**~~：✅ 已修复。`proto/eugraph.thrift` `PropertyType` 枚举新增 `DATETIME_ARRAY=11` / `TIME_ARRAY=12` / `DURATION_ARRAY=13`；`PropertyValueThrift` union 新增对应数组字段；handler 的 `propertyTypeToThrift` 和 `thriftToPropertyValue` 完整支持时间数组转换。
- **`properties(Edge)` + 普通 Expand**: `(a)-[r:TYPE]->(b)` 中 `ExpandPhysicalOp` 不加载边属性，`properties(r)` 返回空 map。变长路径的边属性加载已支持。详见 [query-engine-design.md](../query/engine/query-engine-design.md#十二已知限制与后续规划)
- ~~**PropertyValue 不支持 TemporalValue**~~：✅ 已修复，TemporalValue 拆分为 DateTimeValue/TimeValue/DurationValue 三种类型。
- ~~**紧凑 STRING 格式**~~：✅ 已修复，无分隔符格式已实现。
- ~~**DOUBLE 截断**~~：✅ 已修复，mul/div Duration 支持 double 因子。
- **命名时区不支持**：`datetime('2017-...T23:00+02:00[Europe/Stockholm]')` 中命名时区仅存储名称，不解析为实际偏移量（DST 感知）。需引入 IANA 时区数据库。
