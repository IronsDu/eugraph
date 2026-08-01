## TCK Report — Set/Create TCK Fixes

**Branch:** `feature/set-create-tck-fixes`
**Baseline:** 3018/3897 scenarios (77%) → **Current: 3635/3878 scenarios (94%)**

### Set+Create Summary

| Suite | Scenarios | Passed | Pct |
|-------|-----------|--------|-----|
| Set1 | 11 | **11** | 100% |
| Set2 | 3 | **3** | 100% |
| Set3 | 8 | **8** | 100% |
| Set4 | 5 | **5** | 100% |
| Set5 | 5 | **5** | 100% |
| Set6 | 21 | **21** | 100% |
| Create1 | 20 | **20** | 100% |
| Create2 | 24 | **24** | 100% |
| Create3 | 13 | 12 | 92% |
| Create4 | 2 | **2** | 100% |
| Create5 | 5 | **5** | 100% |
| Create6 | 14 | 12 | 86% |
| **Total** | **131** | **128** | **97%** |

### Remaining Failures (3 scenarios, all pre-existing)

| Scenario | Issue |
|----------|-------|
| Create3[3] | MATCH-CREATE-WITH-CREATE Cartesian product count (12 vs 10) |
| Create6[3] | SKIP/LIMIT short-circuits CREATE side effects |
| Create6[10] | SKIP/LIMIT short-circuits CREATE side effects |

### Changes in This Branch

1. **mutation_mirror.hpp** — In-memory VertexValue/EdgeValue mirroring for mutation operators
2. **SET null 语义** — SET n.p=null ≡ REMOVE; SET += map 中 null 值移除
3. **CREATE 守卫** — VariableAlreadyBound (MATCH vs CREATE-reuse vs cross-clause reuse)
4. **SET 边支持** — SetPhysicalOp 完整 EdgeValue 处理 (SET_PROPERTY / SET_PROPERTIES)
5. **CREATE 边属性 RETURN** — EdgeValue 运行时属性回退解析
6. **SET 多标签** — SET n:Foo:Bar 发射多个单标签 SetItem
7. **CREATE 标签自动创建** — CREATE (:NewLabel) 运行时自动创建标签
8. **属性归属修复** — SET/CREATE 新属性挂到用户标签而非 __anon__
9. **边属性 id 冲突** — 用户属性 "id" 优先于结构字段
10. **RETURN */WITH * 排序** — VERTEX 先于 EDGE (Neo4j 惯例)
11. **错误类型映射** — RequiresDirectedRelationship / InvalidPropertyType
12. **双向箭头解析** — `<-[:TYPE]->` 视为 UNDIRECTED

### Full TCK

| Metric | Before | After |
|--------|--------|-------|
| Scenarios passed | 3018 | **3635** |
| Scenarios failed | 808 | **243** |
| Pass rate | 77% | **94%** |
