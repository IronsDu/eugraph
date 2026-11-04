# 同步扫描回调：从 std::function 到 folly::FunctionRef

> 状态：已确认，随 `perf/evaluator-scratch-reuse` 分支落地。
> 范围：`ISyncGraphDataStore` / `ISyncGraphMetaStore` / `wt::table_scan` /
> `wt::scan_range` 的同步扫描回调。

## 1. 问题

同步扫描接口的回调是：

```cpp
const std::function<bool(VertexId)>& callback
```

`std::function` 是**拥有型**类型擦除容器：

- 小 lambda 通常走 SBO，但超过 SBO 的捕获（多个指针/计数器/大结构）会堆分配；
- 每层转发仍要构造/销毁 `std::function` 对象，并做一次间接调用；
- 接口表达的是“调用期间使用的回调”，却给了“我可能保存它”的所有权语义。

在 `scanEdgesBatch` / 索引扫描这类每批/每条边都会触发的路径上，这属于纯开销。

## 2. 决策

凡**同步、调用期间有效**的扫描回调，改为：

```cpp
folly::FunctionRef<bool(VertexId)> callback
```

`folly::FunctionRef` 是**非拥有 view**：只保存 callable 地址 + 一个函数指针，
不分配、不拷贝 callable。它和 LLVM `function_ref` 同类。

接口：

```cpp
virtual void scanVerticesByLabel(GraphSession& session, LabelId label_id,
                                 folly::FunctionRef<bool(VertexId)> callback) = 0;
virtual void scanEdges(GraphSession& session, VertexId vid, Direction direction,
                       std::optional<EdgeLabelId> label_filter,
                       folly::FunctionRef<bool(const EdgeIndexEntry&)> callback) = 0;
```

实现内部同样用 `folly::FunctionRef` 作为 `ScanCallback`，不再在
`wt_table_ops.cpp` 里落回 `std::function`。

## 3. 生命周期契约（必须写进接口注释）

- 回调只在**本次函数调用期间**被调用；实现不得保存、不得跨协程/线程投递。
- 调用方必须保证 callable 在函数返回前存活：
  - 传 lambda literal：安全（full expression 覆盖调用）；
  - 传局部 `std::function`/捕获对象：安全（调用期间仍在作用域）；
  - 传临时 `std::function`：安全，但仅限本次调用；
  - **禁止**把回调地址交给异步任务后返回。
- 需要“保存回调”的场景（如 `SessionPool::SessionFactory`）继续用拥有型
  `std::function`；`FunctionRef` 不适用。

## 4. 为什么不是其他方案

- **模板 callback**：`ISyncGraphDataStore` 是虚接口，成员函数不能是模板；
  若把接口改成模板会迫使所有实现/调用点头文件化，破坏编译防火墙。
- **`folly::Function`**：仍是拥有型类型擦除（可移动、可保存），没有去掉
  `std::function` 的所有权语义；本场景不需要保存。
- **裸函数指针 + void\***：可行但把类型擦除细节散落到每个实现；`FunctionRef`
  已经封装了同一机制，且与现有 folly 依赖一致。

## 5. 不变量

- 所有 scan callback 仍然返回 `bool`：`false` = 提前停止扫描，语义不变。
- 回调参数仍是 view（`string_view`/`EdgeIndexEntry` 引用）只在调用期间有效；
  这与原 `std::function` 版本的契约一致，不新增生命周期要求。
- WT 错误码路径不变：`table_scan`/`scan_range` 仍返回 WT 错误码，会话路径仍
  `fail_from_wt`。
- 索引 equality/range 的 cursor 接口不受影响；callback 接口只是调用边界变化。

## 6. 测试

- `test_graph_store_wt`：标签扫描、批量邻接扫描、提前 `return false` 停止；
- `test_index_store` / `test_index_e2e`：索引 equality/range/with-value 回调；
- `test_metadata_service`：`ISyncGraphMetaStore` 的 mock override 同步更新；
- 全量 `query_executor_tests`：Expand/LabelScan/IndexScan 的真实调用点。

性能预期：去掉每批/每条边的 `std::function` 构造/堆分配；回调调用仍是间接调用，
不把“FunctionRef”误写成“完全内联”。是否值得进一步批量化，由同二进制 A/B 决定。

## 7. 验证结果（2026-11-03）

- `wt_tests` 7/7、`index_store_tests` 13/13、`metadata_tests` 20/20、
  `index_e2e_tests` 56/56；
- 全量 `query_executor_tests` 614/614（Expand/LabelScan/IndexScan 真实调用点）；
- `FailingMetaStore` 的 `std::function` override 已同步改为 `folly::FunctionRef`。
