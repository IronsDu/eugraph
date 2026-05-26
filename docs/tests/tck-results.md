# TCK 测试结果分类报告

**日期**: 2026-05-27 (更新)
**分支**: feature/temporal-phase2
**总计**: 3897 场景, 16006 步骤
**运行耗时**: 10 分 59 秒
**检测方式**: Parser AST 遍历

---

## 总体结果

| 状态 | 数量 | 说明 |
|------|------|------|
| 步骤通过 | 10659 / 16006 | 66.6% |
| 步骤跳过 | 2416 | 前置步骤失败 |
| 未定义步骤 | 71 | 缺少 step 定义 |
| 步骤失败 | 2860 | 断言失败或查询错误 |
| 场景通过 | ~966 | ~24.8% |

---

## 分类 1: 时间日期构造函数 — ✅ Phase 1 已实现

实现了 6 个构造函数族（`datetime`, `date`, `time`, `localtime`, `localdatetime`, `duration`），支持 MAP<STRING, ANY>、STRING 和 0-arg 调用。返回 STRING 类型（ISO 8601 格式）。

Temporal features 步骤通过数从 0 提升至 2042。剩余失败为 Phase 2 范围（比较、算术、truncate 等）。

| 函数 | 状态 |
|------|------|
| `datetime()` / `date()` / `time()` | ✅ 已实现（MAP 形式；STRING 形式返回 epoch） |
| `localtime()` / `localdatetime()` | ✅ 已实现（MAP 形式；STRING 形式返回 epoch） |
| `duration()` | ✅ 已实现（MAP 形式；STRING 形式返回零时长） |
| `.year` / `.month` 等成员访问器 | ✅ 已实现（Binder 阶段枚举化） |
| 相等比较 (`=`, `<>`) | ✅ 已实现（泛型 Value::operator==） |
| 有序比较 (`<`, `>`, `<=`, `>=`) | ✅ 已实现（同 kind 比较；不同 kind 返回 NULL） |
| 算术运算 (`+`, `-`, `*`, `/`) | ✅ 已实现（temporal±duration, temporal-temporal, duration±*/） |
| `truncate()` / `duration.between()` | Phase 2 |

### 已知限制

- **STRING 形式构造函数**：`date('2024-01-15')` 等字符串参数形式尚未实现解析，当前返回 epoch 默认值（`1970-01-01`）。目前只有 MAP 形式（如 `date({year: 2024, month: 1, day: 15})`）能正确构造指定时间值。
- **DOUBLE 截断**：`duration({months: 1}) * 3.5` 中 DOUBLE 参数会被截断为 `int64_t`（即 `*3`），不支持浮点乘除法。
- **跨 kind 比较**：`date < duration` 等不同 TemporalKind 的比较返回 NULL（符合 Cypher 语义）。
- **`temporal - temporal` 结果**：`datetime - datetime` 等产生的 DURATION 仅包含天和亚天分量（`months=0`），不通过日历反推月份。

---

## 分类 2: AST 精确 SKIP — 不支持的语法

约 520 个场景因查询使用了尚未实现的语法而被 AST 遍历器跳过。

### 2.1 不支持的子句类型

| 子句 | 跳过次数 | 优先级 | 说明 |
|------|---------|--------|------|
| MERGE | 80 | P1 | 合并创建，需要条件扫描+创建逻辑 |
| UNION | 12 | P2 | 查询结果合并 |
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
| 顶点序列化格式 (id + label + props) | TCK 期望格式 |

---

## 优先级汇总

| 优先级 | 分类 | 影响场景数 | 修复路径 |
|--------|------|-----------|---------|
| ~~P0~~ | ~~时间日期构造函数 & 成员访问器 & 比较/算术~~ | ~~~800~~ | ✅ 已实现（Phase 1 + Phase 2） |
| **P0** | `truncate()` / `duration.between()` / STRING 解析 | ~500 | Phase 2 收尾 |
| **P1** | MERGE | ~80 | MERGE 子句实现 |
| **P2** | 无上界变长展开 | ~84 | DFS 无界遍历 |
| **P2** | 布尔类型检查 | ~48 | 完善逻辑运算符的类型推断 |
| **P2** | 结果不匹配 | ~956 | 逐一分析（NULL 语义、排序、精度） |
| **P3** | Parser 限制 | ~250 | Parser 增强 |
| **P3** | 缺失步骤定义 | 71 | 补实现步骤（远期） |
| **P3** | 多标签节点 | 25 | 多标签创建/匹配 |

---

## 已知限制

- **`properties(Edge)` + 普通 Expand**: `(a)-[r:TYPE]->(b)` 中 `ExpandPhysicalOp` 不加载边属性，`properties(r)` 返回空 map。变长路径的边属性加载已支持。详见 [query-engine-design.md](../query/engine/query-engine-design.md#十二已知限制与后续规划)
