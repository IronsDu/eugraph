# Pattern comprehension inside a list comprehension: findings and the open defect

Status: the timeout is fixed; the **element count is still wrong** on a real store,
so a query that depends on it (LDBC complex-10's `commonPostCount`) is not
trustworthy yet. This document is the reference for finishing that work.

Everything below was measured on the sf0.1 store with the server restarted before
each measurement (see "Measurement hazards"), and compared against neo4j on the
same data.

---

## 1. The two failure classes

### 1a. Wrong result: a pattern predicate was not applied at all

```
MATCH (p:Person {id:...})<-[:HAS_CREATOR]-(post:Post)
WITH collect(post) AS posts, p
RETURN size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)]) AS c
-- before: 352 (= size(posts), the filter never ran)
```

Cause: the RETURN/WITH lowering decision inspected only the item's **outermost**
node, so a comprehension wrapped in a call -- `size([x IN ... WHERE <pattern>])`,
which is exactly what complex-10 writes -- was never seen, `pc_asts` stayed empty,
and the item fell through to the row-wise evaluator. Confirmed by an instrumented
run printing `lc=0 pc_asts=0` at the decision point.

**Fixed** by descending through call wrappers to find the comprehension
(`findListComprehensionUnderCalls`) and substituting only that node
(`substituteListComprehensionUnderCalls`), so `size(...)` still returns a length
rather than the list.

### 1b. Wrong result on a real store: the comprehension's loop variable is not what it should be

On sf0.1, with the same query text and only the literal format changed per engine
(neo4j stores LDBC ids as strings):

```
eugraph   {n: 352, c_explicit: 6, c_anon: 0}
neo4j     {n: 352, c_explicit: 3, c_anon: 3}
```

and the anonymous form -- the one complex-10 uses -- collapses to 0, so
`commonPostCount` is wrong.

---

## 2. Root cause of 1b: the loop variable name collides

The discriminating measurement:

```
RETURN size([x IN posts WHERE <pattern>]) AS s1,
       size([x IN posts WHERE <pattern>]) AS s2          -> {s1: 6, s2: 0}
RETURN size([x IN posts WHERE <pattern>]) AS s1,
       size([y IN posts WHERE <pattern>]) AS s2          -> {s1: 6, s2: 6}
-- neo4j gives 6 and 6 for both forms
```

Renaming only the second comprehension's loop variable makes it correct, so the two
comprehensions are semantically independent and the defect is the shared name `x`.

Mechanism: the lowering creates `BoundUnwindOp` with `variable = lc.variable` and
registers it in the context, while a nested pattern comprehension's pattern
variable is also `x` in the common `[x IN posts WHERE (x)-[:R]->(:Y)]` shape.
Planning resolves by name in several places -- `makeSlotLayout` through
`ctx.var_slots`, correlation through `output_schema[pos] != corr.left_var`, and
`bindExistsSubPlan` through `ctx_.lookup(start_var_name)` / `saved_ctx.symbols` --
so the second comprehension's reference to the list resolves into the first one's
Unwind scope. It is then driven per element and handed a vertex instead of the list.

This one cause explains every failure shape that earlier rounds blamed on something
else: two comprehensions over one list returning 0, the nested
`[x IN nodes(p) | size([(x)-->(:Y) | 1])]` case, and complex-10's `c_anon` being 0
with an inflated `c_explicit`.

### Retired explanations (do not re-investigate)

| Blamed earlier | Why it is not the cause |
|---|---|
| Deduplication / element-count semantics | Dedicated probes (one post/one tag, one post/two tags, three posts with overlapping tags, parallel edges, six posts covering six tag configurations) all match neo4j exactly |
| `existence_only` mis-flagged | If it applied, `size()` could only be 0 or 1; it returned 352, then 6 |
| Cache-corrupting hash collision | `ValueHash` collides, but a `std::unordered_set` still compares on equality |
| Slot mis-resolution | Plan-time resolution is correct: `[RES-APPLY] var=posts slot=4 left_column=1 -> pos=2`, and `schema[2]` is `posts` |
| "The second comprehension reads the original list" | It is driven with a single vertex, not the list |
| `need_entire` / property-list narrowing | Measured flat |

---

## 3. The timeout (fixed)

A variant that previously timed out by more than 400 s ran in 0.44 s once fixed, and
its plan lost both `AllNodeScan` and `CrossProduct`. The mechanism:

```
before   CorrelatedSource -\                       after   CorrelatedSource
                           >- CrossProduct                  -> Expand(src=p, ...)
         AllNodeScan(p) --/                                -> Expand -> person
```

`bindExistsSubPlan` only records a correlation pair when the pattern's start
variable resolves in the current scope, and the list lowering restores its saved
context before returning, so a comprehension sitting in its own `WITH` layer was
bound after that restore and its free variable no longer resolved. The planner then
had no way to know it was the list element and fell back to a full scan joined with
the correlated source -- a cartesian product of every node in the store with each
row of the group. That is the whole of the ~180x, and it also explains why a small
synthetic graph looked unaffected: what scales is the store's node count.

**Fixed** by capturing each lowered loop variable's binding before the lowering
discards it, re-registering it around the hoisting pass that binds the inner
pattern comprehension, and erasing it afterwards.

Note this fixed the **timeout only**, not 1b.

---

## 4. Why the obvious fix -- rename the loop variable -- does not work

Three attempts, each blocked further in:

1. renaming at the clone sites broke the pointer-keyed patch map;
2. a shadowing-aware walker was needed, because `[x IN posts | size([(x)-->(:Y) | 1])]`
   has an outer `x` and an inner `x` and renaming the inner one breaks it;
3. carrying the rename through the lowering (unique `__lc_loop_N` for the Unwind
   plus an `outer_vars` filter dropping names only a nested comprehension binds)
   still fails `QueryExecutorTest.PatternComprehensionInsideListComprehension`
   with `PatternComprehensionApply: empty correlation is unsupported`.

The blocker is structural. That test is
`RETURN n.n, [x IN nodes(p) | size([(x)-->(:Y) | 1])]`: the **outer comprehension's
list expression contains a pattern comprehension** whose pattern variable is the
outer loop variable `x`. In the working baseline the outer lowering registers `x`,
so the nested comprehension finds it and correlates to the element column. Renaming
severs exactly that link.

So the name is **load-bearing across a nesting boundary** while being **harmful
across sibling comprehensions**:

* siblings over one list must NOT share the name (the `{6, 0}` defect);
* a nested pattern using the name MUST resolve it to the enclosing element.

A name-level workaround cannot satisfy both.

---

## 5. The fix to implement: stop resolving this by name

The repository's binder identity rule already asks for this:

> `VariableId = SlotId`. Semantic identity is the binding slot the Binder
> allocated; using a variable name or `ScopeId + name` as semantic identity is
> forbidden.

The comprehension lowering is a place where that rule is not yet honoured:

* `bindExistsSubPlan` decides correlation by looking the start variable up by name
  (`ctx_.lookup(start_var_name)`, `saved_ctx.symbols`);
* the Apply's correlation resolution cross-checks by name
  (`output_schema[pos] != corr.left_var`);
* the lowering registers its loop variable by name in `ctx_.symbols`.

Carrying the SlotId through those three paths, instead of the name, separates the
sibling case from the nesting case naturally: siblings have different slots, while
a nested pattern that refers to the enclosing element resolves to the same slot.

Acceptance criteria for that work:

* the discriminating case must go from `{6, 0}` to `{6, 6}`, and the labelled and
  anonymous forms must agree with neo4j (`3` and `3` for person 933);
* `query_executor_tests` stays at 521/521, `optimizer_tests` at 109/109;
* TCK unchanged;
* LDBC complex-10 runs without a timeout **and** returns the same
  `commonPostCount` as an independent route (see below).

A regression test for the sibling case should be added when it passes:
`RETURN size([x IN l WHERE <pattern>]) AS a, size([x IN l WHERE <pattern>]) AS b`.

---

## 6. Verification recipes

### Independent recomputation of complex-10's counts

`commonInterestScore = 2*cpc - pc`, so a self-consistency check catches an obviously
wrong `cpc`. For a real cross-check use a second execution path rather than the
comprehension, e.g.

```
OPTIONAL MATCH (person)-[:HAS_INTEREST]->(t)<-[:HAS_TAG]-(q:Post)<-[:HAS_CREATOR]-(friend)
RETURN count(DISTINCT q)
```

and compare per friend against the comprehension's value. When writing such
queries, do not leave an unbound node pattern such as `(city:City)` in scope: it
multiplies the row count and produces nonsense (56 406 posts instead of 42 were
observed that way).

### Isolated probe instance

The benchmark stores cannot be used for probes: they hold the sf0.1 import, the
test suite writes `:X`/`:Y`/`:L` into whatever instance is running, and
`MATCH (n) DETACH DELETE n` on them exceeds neo4j's transaction memory limit (and
hangs eugraph). Use a standalone neo4j whose database starts empty, so every probe
begins from a genuinely fresh graph:

```
mkdir -p /tmp/n4j-probe/{data,logs,run,conf}
# conf/neo4j.conf: bolt 7699, http 7499, https off, auth off,
#                  data/logs/run under /tmp/n4j-probe, small heap+pagecache
NEO4J_CONF=/tmp/n4j-probe/conf neo4j-admin dbms set-initial-password probepassword123
NEO4J_CONF=/tmp/n4j-probe/conf nohup neo4j console > /tmp/n4j-probe/console.log 2>&1 &
```

---

## 7. Measurement hazards

Two mistakes produced readings that looked like engine differences but were query
errors. Both cost several rounds:

* **different data on the two engines.** Probe both sides on the same graph;
* **inconsistent literal formats.** neo4j stores the LDBC ids as strings, so
  `{id:933}` matches nothing and returns 0 rows, which reads as "eugraph has rows,
  neo4j has none".

Two more:

* **server state.** The same query measured 0.18-0.44 s on a freshly started server
  and timed out on one that had already served a hung query; a run of timeouts left
  even simple controls hanging. Restart the server before each measurement.
* **operator identity in traces.** Tracing the Apply's injected values produced
  lines from several operators at once; without printing the operator's `this`
  pointer they look like one confused operator, and one earlier conclusion ("the
  second comprehension reads col1") came from exactly that conflation.
