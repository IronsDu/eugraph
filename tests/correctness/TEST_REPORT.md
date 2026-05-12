# Cypher Correctness Test Report

**Total scenarios**: 1153
**Runnable**: 19 (1%)
**Skipped**: 1134 (98%)

---

## Skip Categories & Enablement Plan

| Category | Count | Priority | Depends On |
|----------|-------|----------|------------|
| 标签预注册 | 204 | P0 | 测试框架 SetUp 中自动解析 setup/query 中的标签并预注册 |
| 独立 RETURN / WITH 管道 | 453 | P1 | Binder 支持无 MATCH 上下文的 RETURN 和 WITH 管道 |
| 语义错误检测 | 261 | P1 | Binder 完善变量类型冲突、重复绑定等语义检查 |
| UNWIND/IN 关键字 | 138 | P2 | ANTLR grammar 添加 UNWIND、IN 等关键字 |
| DELETE/MERGE/SET/REMOVE | 56 | P2 | Binder 实现对应 DML 子句绑定 |
| 内置函数 | 22 | P2 | 实现 toInteger/toFloat/coalesce 等表达式函数 |

---

## Per-File Summary

| File | Total | Runnable | Skipped |
|------|-------|----------|---------|
| clauses/match-where/MatchWhere1.json | 15 | 0 | 15 |
| clauses/match-where/MatchWhere2.json | 2 | 0 | 2 |
| clauses/match-where/MatchWhere3.json | 3 | 0 | 3 |
| clauses/match-where/MatchWhere4.json | 2 | 0 | 2 |
| clauses/match-where/MatchWhere5.json | 4 | 0 | 4 |
| clauses/match-where/MatchWhere6.json | 8 | 0 | 8 |
| clauses/match/Match1.json | 86 | 1 | 85 |
| clauses/match/Match2.json | 86 | 1 | 85 |
| clauses/match/Match3.json | 30 | 0 | 30 |
| clauses/match/Match4.json | 10 | 0 | 10 |
| clauses/match/Match5.json | 29 | 0 | 29 |
| clauses/match/Match6.json | 97 | 2 | 95 |
| clauses/match/Match7.json | 31 | 0 | 31 |
| clauses/match/Match8.json | 3 | 0 | 3 |
| clauses/match/Match9.json | 9 | 0 | 9 |
| clauses/return-orderby/ReturnOrderBy1.json | 12 | 0 | 12 |
| clauses/return-orderby/ReturnOrderBy2.json | 14 | 0 | 14 |
| clauses/return-orderby/ReturnOrderBy3.json | 1 | 0 | 1 |
| clauses/return-orderby/ReturnOrderBy4.json | 2 | 0 | 2 |
| clauses/return-orderby/ReturnOrderBy5.json | 1 | 0 | 1 |
| clauses/return-orderby/ReturnOrderBy6.json | 5 | 0 | 5 |
| clauses/return-skip-limit/ReturnSkipLimit1.json | 11 | 0 | 11 |
| clauses/return-skip-limit/ReturnSkipLimit2.json | 17 | 0 | 17 |
| clauses/return-skip-limit/ReturnSkipLimit3.json | 3 | 0 | 3 |
| clauses/return/Return1.json | 2 | 0 | 2 |
| clauses/return/Return2.json | 18 | 1 | 17 |
| clauses/return/Return3.json | 3 | 1 | 2 |
| clauses/return/Return4.json | 11 | 5 | 6 |
| clauses/return/Return5.json | 5 | 0 | 5 |
| clauses/return/Return6.json | 21 | 8 | 13 |
| clauses/return/Return7.json | 2 | 0 | 2 |
| clauses/return/Return8.json | 1 | 0 | 1 |
| expressions/aggregation/Aggregation1.json | 2 | 0 | 2 |
| expressions/aggregation/Aggregation2.json | 12 | 0 | 12 |
| expressions/aggregation/Aggregation3.json | 2 | 0 | 2 |
| expressions/aggregation/Aggregation4.json | 0 | 0 | 0 |
| expressions/aggregation/Aggregation5.json | 2 | 0 | 2 |
| expressions/aggregation/Aggregation6.json | 13 | 0 | 13 |
| expressions/aggregation/Aggregation7.json | 0 | 0 | 0 |
| expressions/aggregation/Aggregation8.json | 4 | 0 | 4 |
| expressions/boolean/Boolean1.json | 30 | 0 | 30 |
| expressions/boolean/Boolean2.json | 30 | 0 | 30 |
| expressions/boolean/Boolean3.json | 30 | 0 | 30 |
| expressions/boolean/Boolean4.json | 52 | 0 | 52 |
| expressions/boolean/Boolean5.json | 8 | 0 | 8 |
| expressions/comparison/Comparison1.json | 43 | 0 | 43 |
| expressions/comparison/Comparison2.json | 19 | 0 | 19 |
| expressions/comparison/Comparison3.json | 9 | 0 | 9 |
| expressions/comparison/Comparison4.json | 1 | 0 | 1 |
| expressions/literals/Literals1.json | 6 | 0 | 6 |
| expressions/literals/Literals2.json | 12 | 0 | 12 |
| expressions/literals/Literals3.json | 16 | 0 | 16 |
| expressions/literals/Literals4.json | 10 | 0 | 10 |
| expressions/literals/Literals5.json | 27 | 0 | 27 |
| expressions/literals/Literals6.json | 13 | 0 | 13 |
| expressions/literals/Literals7.json | 20 | 0 | 20 |
| expressions/literals/Literals8.json | 27 | 0 | 27 |
| expressions/mathematical/Mathematical1.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical10.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical11.json | 1 | 0 | 1 |
| expressions/mathematical/Mathematical12.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical13.json | 1 | 0 | 1 |
| expressions/mathematical/Mathematical14.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical15.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical16.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical17.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical2.json | 1 | 0 | 1 |
| expressions/mathematical/Mathematical3.json | 1 | 0 | 1 |
| expressions/mathematical/Mathematical4.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical5.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical6.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical7.json | 0 | 0 | 0 |
| expressions/mathematical/Mathematical8.json | 2 | 0 | 2 |
| expressions/mathematical/Mathematical9.json | 0 | 0 | 0 |
| expressions/null/Null1.json | 17 | 0 | 17 |
| expressions/null/Null2.json | 17 | 0 | 17 |
| expressions/null/Null3.json | 10 | 0 | 10 |
| expressions/pattern/Pattern1.json | 39 | 0 | 39 |
| expressions/pattern/Pattern2.json | 11 | 0 | 11 |
| expressions/precedence/Precedence1.json | 72 | 0 | 72 |
| expressions/precedence/Precedence2.json | 26 | 0 | 26 |
| expressions/precedence/Precedence3.json | 11 | 0 | 11 |
| expressions/precedence/Precedence4.json | 12 | 0 | 12 |