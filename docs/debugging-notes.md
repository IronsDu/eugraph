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

---

## 案例 4：RPC 客户端 EventBase 冲突导致死锁

### 现象

RPC 客户端调用任何方法（`createLabel`、`executeCypher` 等）时程序卡住（死锁），不返回也不报错。

### 排查过程

1. 使用 GDB attach 到卡住的进程，发现所有线程都在 `pthread_cond_wait` 上等待。
2. 检查线程栈：一个线程在 `folly::EventBase::loop()` 内等待，另一个在 `sync_createLabel` → `get()` 上等待。
3. 分析 fbthrift 客户端 API 的 EventBase 依赖关系。

### 根因

fbthrift 客户端有两种调用模式：

- **`sync_*` 方法**：内部调用 `evb->loop()` 驱动 EventBase 完成单次 RPC，然后返回。
- **`semifuture_*` 方法**：返回一个 `SemiFuture`，需要通过 `.via(evb).get()` 手动调度。

问题在于：当 `EventBase` 已经在另一个线程通过 `loopForever()` 运行时，`sync_*` 方法内部的 `loop()` 调用会产生冲突（EventBase 不支持嵌套 loop），导致死锁。

### 修复

将所有 RPC 调用从 `sync_*` 改为 `semifuture_*` + `.via(evb).get()` 模式：

```cpp
// 错误：sync_* 与 loopForever() 冲突
auto result = client_->sync_createLabel(name, props);

// 正确：semifuture_* + via + get
auto result = client_->semifuture_createLabel(name, props).via(evb_.get()).get();
```

EventBase 在构造时就启动后台线程运行 `loopForever()`：

```cpp
EuGraphRpcClient::EuGraphRpcClient(const std::string& host, int port)
    : host_(host), port_(port), evb_(std::make_unique<folly::EventBase>()),
      evb_thread_([this] { evb_->loopForever(); }) {
    evb_->waitUntilRunning();
}
```

### 经验

1. **fbthrift `sync_*` 方法会内部驱动 EventBase**——不能在已有 `loopForever()` 的 EventBase 上使用。
2. **正确模式是 `semifuture_*` + `.via(evb).get()`**——EventBase 在后台线程持续运行，RPC 通过 `.via()` 调度到该线程。
3. **`evb_->runInEventBaseThreadAndWait()` 也不要混用**——只在初始化阶段（如 `connect()`）使用，常规 RPC 应走 `semifuture` 路径。

---

## 案例 5：`co_executeCypher` 中 EventBase 调度与嵌入式模式冲突

### 现象

commit `931b2db3ffd8a84f6a51573dfa6b5d4b7b800459` 修改了 `EuGraphHandler::co_executeCypher`，添加了 `co_withExecutor` 调度到 IO 线程。此修改导致嵌入式模式（shell 直连存储层，不经过 RPC）出现死锁。

### 根因

在 RPC 模式下，fbthrift 的 IO 线程有运行中的 EventBase（`evb->isRunning() == true`），`co_withExecutor(ioEvb)` 可以正常调度。但在嵌入式模式下，handler 直接调用 `executeCypher`，没有 fbthrift IO 线程，`ioEvb->isRunning() == false`，此时 `co_withExecutor` 会尝试驱动一个未运行的 EventBase，导致死锁。

### 修复

在 `co_withExecutor` 调度前检查 EventBase 是否在运行：

```cpp
// 只有当 EventBase 在运行时才调度到 IO 线程
if (ioEvb->isRunning()) {
    co_await co_withExecutor(ioEvb, innerTask);
} else {
    co_await innerTask;  // 嵌入式模式：直接在当前线程执行
}
```

### 与案例 4 的关系

两个问题本质相同——都是 **fbthrift 的 EventBase 管理与执行模式不匹配**：

- **案例 4（客户端）**：`sync_*` 内部调用 `evb->loop()`，与后台 `loopForever()` 冲突。解决方式：用 `semifuture_*` 避免内部 loop。
- **案例 5（服务端）**：`co_withExecutor` 假设 EventBase 已在运行，但嵌入式模式不满足。解决方式：检查 `isRunning()` 后决定是否调度。

### 经验

1. **fbthrift 的 EventBase 有两种角色**：被 `loopForever()` 驱动的"常驻模式"和被 `loop()` 驱动的"按需模式"。两者不能混用。
2. **嵌入式模式与 RPC 模式的区别**：嵌入式没有 IO 线程池，没有运行中的 EventBase，所有协程在调用方线程同步执行。代码必须同时兼容两种模式。
3. **`isRunning()` 是判断 EventBase 状态的关键 API**——在调度前检查可以避免两种模式的不兼容。
