# 查询错误模型（错误分类 → Neo4j 状态码）

> 上游问题：《测试报告_类型与运算_BUG记录.md》BUG-12（状态码格式非标准）、BUG-13（分类错误）、BUG-03（map 参数未做类型校验）。
> 判定基准是 neo4j 5.26：状态码与消息均以实测为准（开发期用一次性对照脚本逐条打两边，
> 期望值已固化进 `tests/test_query_executor.cpp` 的 `Error*` 用例）。

## 1. 问题

Bolt 的 `FAILURE` 消息带 `code` + `message` 两个字段，客户端（驱动）按 `code` 决定异常类型：

| 现状 | 后果 |
|---|---|
| 所有查询失败都发 `code="DatabaseError"` | 客户端无法区分「写错了语句」与「服务端执行失败」；上层产品只能从 `message` 文本里猜分类，于是出现 `gdmbase...SyntaxError` 这类非标准状态码 |
| 错误分类只体现在消息文本前缀里（`"Binding failed; SyntaxError: ..."`） | 分类信息没有结构化通道，前端各自解析字符串 |

同时若干运行期失败**根本没有报错**（`1/0` 返回 NULL、INT64 溢出静默回绕、`date({year:'2024'})` 按默认值构造），
它们不是「状态码不对」，而是「用错参数拿到了一个看似正常的结果」，危害更大。

## 2. 机制

分类只定义一次，落在 `src/common/types/query_error.hpp`：

```cpp
enum class QueryErrorKind : uint8_t { Syntax, Type, Argument, Arithmetic, ExecutionFailed };
const char* queryErrorToken(QueryErrorKind);       // "SyntaxError" / "TypeError" / ...
const char* neo4jStatusCode(QueryErrorKind);       // "Neo.ClientError.Statement.SyntaxError" / ...
QueryErrorKind classifyQueryErrorMessage(std::string_view);
class QueryException : public std::runtime_error;  // kind + message
```

| 分类 | token | Neo4j 状态码 |
|---|---|---|
| `Syntax` | `SyntaxError` | `Neo.ClientError.Statement.SyntaxError` |
| `Type` | `TypeError` | `Neo.ClientError.Statement.TypeError` |
| `Argument` | `ArgumentError` | `Neo.ClientError.Statement.ArgumentError` |
| `Arithmetic` | `ArithmeticError` | `Neo.ClientError.Statement.ArithmeticError` |
| `ExecutionFailed` | `ExecutionFailed` | `Neo.DatabaseError.Statement.ExecutionFailed` |

分类 token 沿用仓库既有约定（错误文本里的 `SyntaxError:` / `TypeError:` 前缀）：TCK 的分类器
（`tests/tck/tck_context.cpp`）按这些 token 判定「应当抛什么错误」，因此 token 是既有兼容面，不改名。

### 2.1 两条错误通道

1. **编译期（解析/绑定）**：`QueryExecutor::prepareStream()` 把失败原因写进 `ctx->error`（文本里已带 token）。
   `GraphService::executeCypher()` 用 `classifyQueryErrorMessage()` 翻译成分类后抛 `QueryException`。
2. **运行期**：函数/算子直接抛异常，向上穿过算子生成器到达 Bolt/Thrift 的 `catch`：
   * 新代码抛 `QueryException(kind, message)`，分类随异常传递，无需解析文本；
   * 历史代码写的是 `throw std::runtime_error("TypeError: ...")`，Bolt 侧 `makeFailureFor()`
     用同一个 `classifyQueryErrorMessage()` 兜底分类（一处解释，而不是每个前端各猜一次）。
     认不出 token 的运行期异常归 `ExecutionFailed`（如取消、断连、事务失败）。

### 2.2 消息契约

`QueryException::what()` 带 token 前缀（`"ArithmeticError: long overflow"`），`message()` 不带前缀：

* Bolt `FAILURE.message` 用 `message()` —— 与 neo4j 的措辞一致（neo4j 就是 `long overflow`）；
* Thrift 只传异常文本（IDL 里没有 code 字段），沿用 `what()`，既有前端与 TCK 的文本分类不受影响。

## 3. 覆盖的语义（全部实测自 neo4j 5.26）

| 语句 | neo4j 状态码 | 消息 |
|---|---|---|
| `RETURN 9223372036854775807 + 1` | ArithmeticError | `long overflow` |
| `RETURN 9223372036854775807 * 2` / `-x - 2` / `-(-x - 1)` | ArithmeticError | `long overflow` |
| `RETURN 1 / 0`、`RETURN 5 % 0` | ArithmeticError | `/ by zero` |
| `RETURN (-9223372036854775807 - 1) / -1` | （不报错） | 回绕得到 `-9223372036854775808` |
| `RETURN (-9223372036854775807 - 1) % -1` | （不报错） | `0` |
| `RETURN toInteger('9223372036854775808')` | TypeError | `integer, 9223372036854775808, is too large` |
| `RETURN toInteger(1e30)` | （不报错） | `9223372036854775807`（Java `(long)` 饱和语义） |
| `RETURN substring('hello', -2)` | ExecutionFailed | `Cannot handle negative start index nor negative length` |
| `RETURN date({year:'2024'})` | ExecutionFailed | `year must be an integer value, but was a UTF8StringValue` |
| `RETURN date({year:2024, bogus:1})` | ArgumentError | `No such field: bogus` |
| `RETURN duration({days:1, bogus:1})` | ExecutionFailed | `Unknown field: bogus` |
| `RETURN date({year:1000000000})` | ArgumentError | `Invalid value for Year (valid values -999999999 - 999999999): 1000000000` |
| `RETURN date({year:2024, month:13, day:1})` | ArgumentError | `Invalid value for MonthOfYear (valid values 1 - 12): 13` |
| `RETURN date.truncate('hour', date('2024-06-15'))` | TypeError | `Unit too small for truncation: Hours` |
| `RETURN time.truncate('week', time('12:34:56'))` | TypeError | `Unit is too large to be used for truncation` |
| `RETURN time('12:00:00[UTC]')` | ArgumentError | `Using a named time zone e.g. [UTC] is not valid for a time without a date. ...` |

**`INT64_MIN / -1` 曾经直接打死服务进程**：C++ 里 `INT64_MIN / -1` 与 `% -1` 是未定义行为，
x86 上触发 SIGFPE（integer divide by zero），一条 Cypher 就能让服务退出。
现在按 neo4j 的语义显式处理（`/` 回绕为 `INT64_MIN`，`%` 为 `0`），
整数除法/取模全部走 `checkedDivide`/`checkedModulo`。

## 4. 与 neo4j 的差异（有意保留或尚未覆盖）

* **`date - date` 保留**：neo4j 报 `Type mismatch`，我们支持并使其恒等于 `duration.between`
  （见 [temporal-semantics.md](temporal-semantics.md)）。
* **`point()` 未实现**（本次范围外）：`point({...})` 在两边都报错，但我们报 `SyntaxError: Function not found`，
  neo4j 报 `ArgumentError: Unknown coordinate reference system`。用户已明确 point/CRS 不在本次范围内。
* **map 参数的结构性规则未实现**：neo4j 另有 `year must be specified`、
  `second cannot be specified without minute`、`Cannot assign month to week date`、
  `Cannot assign time zone if also assigning other fields.`、`Not supported: epochSeconds` 等规则；
  我们只做「类型 / 取值范围 / 未知字段」三类校验，其余仍按既有组合逻辑处理。
* **map 里显式 NULL**：neo4j 把 `{year:null}` 当 0（`toString` 得 `0000-01-01`），我们仍按「未指定」用默认 1970-01-01。
* **日期字符串的月/日越界**：`date('2024-13-01')` neo4j 报 `SyntaxError: Invalid value for MonthOfYear...`，
  我们仍会顺延为 `2025-01-01`（年份越界已按 `Text cannot be parsed to a Date` 处理）。
* **`+` 的类型强转**：neo4j 里 `RETURN 1 + 'a'` 得到字符串 `1a`，我们在绑定期报
  `SyntaxError: Cannot apply + to INT64 and STRING`（更严格，未放宽）。
* **`localtime('12:00:00[UTC]')`**：neo4j 报 `SyntaxError: Text cannot be parsed to a LocalTime`，
  我们的 localtime 字符串形式仍忽略时区段（本次只收紧了 `time()`）。

## 5. TCK 影响

`tck_tests` 的比较是**文本**比较，值文本由 `temporalToIsoString`（Thrift/TCK 渲染）产生。
BUG-04 修好 `toString()` 的秒分量后，值文本一度跟着变成 `12:00:00`，与 openCypher TCK 的期望文本
（`12:00`）不一致，TCK 场景从 3844 掉到 3427。修法是把两种渲染分开（见
[temporal-semantics.md](temporal-semantics.md) 第 5、8 节）：`toString()` 保持 neo4j 语义，
Thrift/TCK 文本走 ISO 约定。`Temporal10` 的 8 个场景由 duration 负亚秒归一化修复（见该文档第 8 节）；`Temporal3` 的
96 个场景是既有未实现能力（`localdatetime({..., time: <localtime>})` 的 time 基准键），与本阶段无关。

## 6. 二进制（bytes）参数与属性（BUG-11，已支持）

Cypher 没有二进制字面量，二进制只能从参数或属性进来。打通的位置：

| 层 | 处理 |
|---|---|
| 内部值 | `BytesValue`（`src/query/dataset/row.hpp`）加入 `Value`；`PropertyValue` 增加 `std::vector<uint8_t>` |
| KV 编码 | `ValueCodec` 新标签 `0x0E`（`[tag][4B 长度][原始字节]`）；`PropertyType::BYTES` 追加在 `ANY` 之后，既有取值序号不变 |
| Bolt | 出参 `valueToBolt`/`propertyToBolt` 走 `Bytes`（`0xCC/0xCD/0xCE`）；入参 `boltParamToValue` 解 `Bytes` 为 `BytesValue` |
| Thrift | IDL 的 `ResultValue` 是既有兼容面，不新增字段：二进制以**十六进制文本**给出（值本身与顶点/边 JSON 里的属性都是），Bolt 客户端拿到的仍是真正的 Bytes |
| 语义 | 相等按内容比较（`valueEquals` 走 `BytesValue::operator==`）；排序不定义（与其它不可排序值同组），neo4j 同样只支持判等 |

回归：`tests/test_query_executor.cpp` 的 `BytesParamsAndPropertiesRoundtrip`（参数回传、写入后读回、
判等、SET 覆盖写），期望值实测自 neo4j。

实现时踩到的一个机制问题（已一并修掉）：`Value → PropertyValue` 原先有三份实现
（`property_value_convert.hpp`、`set_physical_op.cpp` 的匿名命名空间副本、`physical_planner.cpp` 的
static 简化副本）。CREATE 走的是头文件那份、SET 走的是 set 那份、常量属性走 planner 那份 ——
只改一处就会出现"SET 能写、CREATE 丢值"这种按路径而异的行为。现在只保留 `property_value_convert.hpp`
一份，另外两处删除（planner 那份连 list 都不支持，属顺带修好的同类缺陷）。

另外注意：旧版本二进制会把二进制参数写成"类型 ANY、值为 NULL"的属性定义；这种历史数据读出来是
NULL（`n.blob` 在极端情况下会退化成节点自身），属旧数据残留，新建的属性不受影响。

## 7. 验证

* 期望值来源：每条断言都用同一语句在 neo4j 5.26 上实测过（BUG-12/13 的状态码逐条对照，
  含 `date({year:1000000000})`、`toInteger` 溢出、`substring` 负参、`truncate` 单位等）。
* `tests/test_query_executor.cpp`：`QueryErrorTest.*`（分类与状态码映射）+
  `QueryExecutorTest.Error*`（上表逐条断言，含 SIGFPE 回归用例）；
  `QueryExecutorTest.ElementIdReturnsStringId` / `TrimWithCharacterSet`（BUG-15/16）。
