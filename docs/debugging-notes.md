# 开发调试笔记

> 参见 [overview.md](overview.md) 返回文档导航

本文档记录开发过程中遇到的典型问题和排查经验，供后续开发参考。每个案例包含现象、排查过程、根因和经验总结。

---

## 案例 1：use-after-move 导致聚合查询 SEGV

### 现象

`sum(n.age)` 查询段错误，但 `count(*)` 和 `count(*) GROUP BY n.city` 均正常。

### 排查过程

1. 首先用 ASAN 构建运行测试，得到精确的崩溃栈：
   ```
   #0 std::variant<...>::index() const /usr/include/c++/13/variant:1625
   #1 std::__do_visit<..., expressionToString>
   #2 expressionToString(...)
   ```
2. 崩溃地址为 `0x000000000008`，即偏移 8 处的空指针访问。
3. 追溯调用链：`expressionToString` 对 FunctionCall 递归遍历 `args`，而 `args[0]` 已经是一个 moved-from 的 null `unique_ptr`。

### 根因

`logical_plan_builder.cpp` 中构建聚合算子时，语句顺序有误：

```cpp
// 修复前（错误）
af.arg = std::move(fc->args[0]);                    // ① 移动 args[0]
std::string name = retItem.alias.value_or(           // ② expressionToString 立即求值
    cypher::expressionToString(retItem.expr));        //    遍历 fc->args[0] → 空指针
```

关键点：**`std::optional::value_or()` 的参数是立即求值的**，即使 `retItem.alias` 有值，`expressionToString` 也会被调用。

### 修复

将 name 计算移到 move 之前：

```cpp
// 修复后（正确）
std::string name = retItem.alias.value_or(
    cypher::expressionToString(retItem.expr));        // ① 先计算 name
af.arg = std::move(fc->args[0]);                     // ② 再移动
```

### 经验

1. **`value_or` 的参数是立即求值的**——不要以为 optional 有值就不会执行参数表达式。如果参数有副作用或依赖已被移动的对象，就会出问题。安全写法：`alias.has_value() ? *alias : expressionToString(expr)`
2. **移动语义要保证"先读后移"**——在 `std::move` 之后，源对象处于 valid-but-unspecified 状态，不应再读取其内容。
3. **ASAN 构建是排查内存问题的利器**——SEGV 的崩溃地址（如 `0x000000000008`）和栈追踪能直接定位到空指针解引用的准确位置。

---

## 案例 2：prop_id 从 1 开始而非 0

### 现象

聚合查询中 `count(*)` 正常返回，但 `sum(n.age)` 等需要读取属性的聚合返回 null 或错误结果。

### 排查过程

1. 在聚合执行路径中加日志，发现表达式求值返回 null（`std::monostate`）。
2. 检查属性求值逻辑：`evalPropertyAccess` 通过 `label_defs` 查找 prop_name → prop_id 映射，再用 prop_id 索引 `properties` 向量。
3. 发现 metadata store 的 ID 分配逻辑：
   ```cpp
   // async_graph_meta_store.cpp:76
   uint16_t prop_id = 1;  // 从 1 开始！
   ```
4. 测试代码中用 `props[0]` 和 `props[1]` 存属性，但实际 prop_id 为 1 和 2。

### 根因

Metadata store 分配 prop_id 从 **1** 开始，而非习惯性假设的 0。测试中创建 `Properties` 向量时按从 0 开始的索引存储，导致 prop_id 不匹配，属性读取始终返回 null。

### 修复

测试中正确按 prop_id 索引存储属性：

```cpp
Properties props;
props.resize(3);        // 索引 0 不使用，1=age，2=city
props[1] = PropertyValue(p.age);
props[2] = PropertyValue(p.city);
```

### 经验

1. **不要假设 ID 从 0 开始**——分配逻辑可能从任意值开始，应先确认 `createLabel` 的返回值和 ID 分配代码。
2. **Properties 向量是 prop_id 索引的稀疏数组**——跳过索引 0 是正常的，resize 时要按最大 prop_id + 1 来分配。

---

## 案例 3：协程中引用的生命周期

### 现象

在 folly 协程中通过引用传入的外部对象，在 `co_await` 后访问时可能已失效。

### 根因

folly::coro 的 `co_await` 可能将执行调度到 IO 线程池，协程帧中持有的引用指向的原始对象可能在调用方栈帧销毁后变为悬挂引用。

### 经验

1. **传入协程的引用要确保生命周期覆盖整个协程执行**——包括所有 `co_await` 点。
2. **协程按值捕获或 shared_ptr 比引用更安全**——尤其是跨线程调度场景。
3. **QueryExecutor 中的做法**：使用 `co_invoke` 创建内部协程，将 `this` 指针和引用参数通过 lambda 捕获传入，确保在 `co_withExecutor` 调度前对象仍存活。
