# TCK Correctness Test Cases

**Extracted**: 1153 scenarios  
**Skipped**: 0 scenarios  
**Feature files**: 220  
**Source**: openCypher TCK (Apache 2.0)  

---

## Match1 (86/86 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match1 - Match nodes-[1] Match non-existent nodes returns empty` | QUERY | [1] Match non-existent nodes returns empty |
| 2 | `Match1 - Match nodes-[2] Matching all nodes` | QUERY | [2] Matching all nodes |
| 3 | `Match1 - Match nodes-[3] Matching nodes using multiple labels` | QUERY | [3] Matching nodes using multiple labels |
| 4 | `Match1 - Match nodes-[4] Simple node inline property predicate` | QUERY | [4] Simple node inline property predicate |
| 5 | `Match1 - Match nodes-[5] Use multiple MATCH clauses to do a Cartesian product` | QUERY | [5] Use multiple MATCH clauses to do a Cartesian product |
| 6 | `Match1 - Match nodes-[6] Fail when using parameter as node predicate in MATCH` | ERROR | [6] Fail when using parameter as node predicate in MATCH |
| 7 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 8 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[r]->()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 9 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()<-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 10 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=(), ()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 11 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[r]-(), ()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 12 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-(), ()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 13 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 14 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[]-(), ()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 15 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[]-(), ()-[r]-(), ()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 16 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[]-(), (), ()-[r]-()]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 17 | `Match1 - Match nodes-[7] Fail when a relationship has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), (s), (s)-[r]->(t)<-[]-(b)]` | ERROR | [7] Fail when a relationship has the same variable in a preceding MATCH [pattern |
| 18 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()- |
| 19 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[]->()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()- |
| 20 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()<-[]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()< |
| 21 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[*]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()- |
| 22 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[*]->()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()- |
| 23 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()-[]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = |
| 24 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()-[]->()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = |
| 25 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()<-[]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = |
| 26 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()-[*]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = |
| 27 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()-[*]->()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = |
| 28 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]-(), r = ()-[]-(), ()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]-( |
| 29 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[]-(), ()-[]-(), ()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()- |
| 30 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]-()<-[]-(), r = ()-[]-()]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]-( |
| 31 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), r = (a)-[q]-(b), (s)-[p]-(t)-[]-(b)]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), r  |
| 32 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), r = (s)-[p]-(t)-[]-(b)]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a |
| 33 | `Match1 - Match nodes-[8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), r = (s)-[p]->(t)<-[]-(b)]` | ERROR | [8] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a |
| 34 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 35 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]->(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 36 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()<-[r]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 37 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-()-[]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 38 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r*]-()-[]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 39 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 40 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]->(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 41 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()<-[r]-(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 42 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(), (r)-[]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 43 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(), ()-[]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 44 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=(s)-[r]-(t), (r)-[]-(t)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 45 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=(s)-[r]-(t), (s)-[]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 46 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=(), ()-[r]-(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 47 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(), (), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 48 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[r]-(), (r), ()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 49 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-(), ()-[r]-(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 50 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[r]-(), ()-[]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 51 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[]-(), ()-[r]-(), (r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 52 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[r]-(), (r), ()-[]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 53 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[r]-(), (), (r)-[]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 54 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[r*]-(), (r), ()-[]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 55 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[*]-()-[r]-(), (), (r)-[]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 56 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[*]-()-[r]-(), (), (r)-[*]-()]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 57 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=()-[*]-()-[r]-(), (), ()-[*]-(r)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 58 | `Match1 - Match nodes-[9] Fail when a relationship has the same variable in the same pattern [pattern=(x), (a)-[r]-(b), (s), (s)-[]->(r)<-[]-(b)]` | ERROR | [9] Fail when a relationship has the same variable in the same pattern [pattern= |
| 59 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()-[]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 60 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()-[]->(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 61 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()<-[]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()< |
| 62 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()-[*]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 63 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()-[*]->(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 64 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(), r = ()-[]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(), r = |
| 65 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(), r = ()-[]->(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(), r = |
| 66 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(), r = ()<-[]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(), r = |
| 67 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(), r = ()-[*]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(), r = |
| 68 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(), r = ()-[*]->(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(), r = |
| 69 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=()-[]-(), r = ()-[]-(), (), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=()-[]-( |
| 70 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=r = ()-[]-(), ()-[]-(), (), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 71 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=()-[]-()<-[]-(), r = ()-[]-(), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=()-[]-( |
| 72 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), r = (a)-[q]-(b), (s)-[p]-(t)-[]-(b), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), r  |
| 73 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), (a)-[q]-(b), r = (s)-[p]-(t)-[]-(b), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), (a |
| 74 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), (a)-[q]-(b), r = (s)-[p]->(t)<-[]-(b), (r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), (a |
| 75 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), r = (s)-[p]-(t)-[]-(b), (r), (a)-[q]-(b)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), r  |
| 76 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), r = (s)-[p]->(t)<-[]-(b), (r), (a)-[q]-(b)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), r  |
| 77 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), r = (s)-[p]-(t)-[]-(b), (a)-[q]-(r)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), r  |
| 78 | `Match1 - Match nodes-[10] Fail when a path has the same variable in the same pattern [pattern=(x), r = (s)-[p]->(t)<-[]-(b), (r)-[q]-(b)]` | ERROR | [10] Fail when a path has the same variable in the same pattern [pattern=(x), r  |
| 79 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid=true]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid=true] |
| 80 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid=123]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid=123] |
| 81 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid=123.4]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid=123.4] |
| 82 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid='foo']` | ERROR | [11] Fail when matching a node variable bound to a value [invalid='foo'] |
| 83 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid=[]]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid=[]] |
| 84 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid=[10]]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid=[10]] |
| 85 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid={x: 1}]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid={x: 1}] |
| 86 | `Match1 - Match nodes-[11] Fail when matching a node variable bound to a value [invalid={x: []}]` | ERROR | [11] Fail when matching a node variable bound to a value [invalid={x: []}] |

## Match2 (86/86 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match2 - Match relationships-[1] Match non-existent relationships returns empty` | QUERY | [1] Match non-existent relationships returns empty |
| 2 | `Match2 - Match relationships-[2] Matching a relationship pattern using a label predicate on both sides` | QUERY | [2] Matching a relationship pattern using a label predicate on both sides |
| 3 | `Match2 - Match relationships-[3] Matching a self-loop with an undirected relationship pattern` | QUERY | [3] Matching a self-loop with an undirected relationship pattern |
| 4 | `Match2 - Match relationships-[4] Matching a self-loop with a directed relationship pattern` | QUERY | [4] Matching a self-loop with a directed relationship pattern |
| 5 | `Match2 - Match relationships-[5] Match relationship with inline property value` | QUERY | [5] Match relationship with inline property value |
| 6 | `Match2 - Match relationships-[6] Match relationships with multiple types` | QUERY | [6] Match relationships with multiple types |
| 7 | `Match2 - Match relationships-[7] Matching twice with conflicting relationship types on same relationship` | QUERY | [7] Matching twice with conflicting relationship types on same relationship |
| 8 | `Match2 - Match relationships-[8] Fail when using parameter as relationship predicate in MATCH` | ERROR | [8] Fail when using parameter as relationship predicate in MATCH |
| 9 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)] |
| 10 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 11 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]->()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 12 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)<-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)<-[] |
| 13 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 14 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]->(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-> |
| 15 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()<-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()<-[]- |
| 16 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 17 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]->(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 18 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)<-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)<-[] |
| 19 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-()-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 20 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(r)-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 21 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-()-[*]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 22 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(r)-[*]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 23 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r), ()-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r), () |
| 24 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-(), ()-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 25 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(r), ()-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 26 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(), (r)-[]-()]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 27 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(), ()-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-( |
| 28 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]-(t), (s)-[]-(t)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(r)-[]- |
| 29 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]-(r), (s)-[]-(t)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]- |
| 30 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]-(t), (r)-[]-(t)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]- |
| 31 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]-(t), (s)-[]-(r)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s)-[]- |
| 32 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a)-[q]-(b), (r), (s)-[]-(t)-[]-(b)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a |
| 33 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a)-[q]-(b), (r), (s)-[]->(t)<-[]-(b)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a |
| 34 | `Match2 - Match relationships-[9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a)-[q]-(b), (t), (s)-[]->(r)<-[]-(b)]` | ERROR | [9] Fail when a node has the same variable in a preceding MATCH [pattern=(s), (a |
| 35 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 36 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[]->()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 37 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()<-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 38 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[*]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 39 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[*]->()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 40 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()<-[*]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 41 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[p*]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 42 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()-[p*]->()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 43 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=r = ()<-[p*]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=r = () |
| 44 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=(), r = ()-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=(), r  |
| 45 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]-(), r = ()-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]- |
| 46 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]->(), r = ()<-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[]- |
| 47 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=()<-[]-(), r = ()-[]->()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=()<-[] |
| 48 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[*]->(), r = ()<-[]-()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=()-[*] |
| 49 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=()<-[p*]-(), r = ()-[*]->()]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=()<-[p |
| 50 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), (r), (s)-[]->(t)<-[]-(b)]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), ( |
| 51 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), r = (s)-[p]->(t)<-[]-(b)]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), ( |
| 52 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q*]-(b), r = (s)-[p]->(t)<-[]-(b)]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), ( |
| 53 | `Match2 - Match relationships-[10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), (a)-[q]-(b), r = (s)-[p*]->(t)<-[]-(b)]` | ERROR | [10] Fail when a path has the same variable in a preceding MATCH [pattern=(x), ( |
| 54 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r] |
| 55 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r]->()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r] |
| 56 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)<-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)<-[r |
| 57 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r]-(r)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r] |
| 58 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r]->(r)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[r] |
| 59 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)<-[r]-(r)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)<-[r |
| 60 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]-()-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]- |
| 61 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=()-[]-(r)-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 62 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]-()-[r*]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]- |
| 63 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=()-[]-(r)-[r*]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 64 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r), ()-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r), () |
| 65 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]-(), ()-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]- |
| 66 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=()-[]-(r), ()-[r]-()]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 67 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]-(t), (s)-[r]-(t)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r)-[]- |
| 68 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(s)-[]-(r), (s)-[r]-(t)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(s)-[]- |
| 69 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r), (a)-[q]-(b), (s), (s)-[r]-(t)-[]-(b)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r), (a |
| 70 | `Match2 - Match relationships-[11] Fail when a node has the same variable in the same pattern [pattern=(r), (a)-[q]-(b), (s), (s)-[r]->(t)<-[]-(b)]` | ERROR | [11] Fail when a node has the same variable in the same pattern [pattern=(r), (a |
| 71 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=r = ()-[]-(), ()-[r]-()]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 72 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=r = ()-[]-(), ()-[r*]-()]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=r = ()- |
| 73 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=r = (a)-[p]-(s)-[]-(b), (s)-[]-(t), (t), (t)-[r]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=r = (a) |
| 74 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=r = (a)-[p]-(s)-[]-(b), (s)-[]-(t), (t), (t)-[r*]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=r = (a) |
| 75 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=r = (a)-[p]-(s)-[*]-(b), (s)-[]-(t), (t), (t)-[r*]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=r = (a) |
| 76 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p]-(s)-[]-(b), r = (s)-[]-(t), (t), (t)-[r*]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p] |
| 77 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p]-(s)-[]-(b), r = (s)-[*]-(t), (t), (t)-[r]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p] |
| 78 | `Match2 - Match relationships-[12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p]-(s)-[]-(b), r = (s)-[*]-(t), (t), (t)-[r*]-(b)]` | ERROR | [12] Fail when a path has the same variable in the same pattern [pattern=(a)-[p] |
| 79 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid=true]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid=true] |
| 80 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid=123]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid=123] |
| 81 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid=123.4]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid=123.4] |
| 82 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid='foo']` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid='foo'] |
| 83 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid=[]]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid=[]] |
| 84 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid=[10]]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid=[10]] |
| 85 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid={x: 1}]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid={x: 1} |
| 86 | `Match2 - Match relationships-[13] Fail when matching a relationship variable bound to a value [invalid={x: []}]` | ERROR | [13] Fail when matching a relationship variable bound to a value [invalid={x: [] |

## Match3 (30/30 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match3 - Match fixed length patterns-[1] Get neighbours` | QUERY | [1] Get neighbours |
| 2 | `Match3 - Match fixed length patterns-[2] Directed match of a simple relationship` | QUERY | [2] Directed match of a simple relationship |
| 3 | `Match3 - Match fixed length patterns-[3] Undirected match on simple relationship graph` | QUERY | [3] Undirected match on simple relationship graph |
| 4 | `Match3 - Match fixed length patterns-[4] Get two related nodes` | QUERY | [4] Get two related nodes |
| 5 | `Match3 - Match fixed length patterns-[5] Return two subgraphs with bound undirected relationship` | QUERY | [5] Return two subgraphs with bound undirected relationship |
| 6 | `Match3 - Match fixed length patterns-[6] Matching a relationship pattern using a label predicate` | QUERY | [6] Matching a relationship pattern using a label predicate |
| 7 | `Match3 - Match fixed length patterns-[7] Matching nodes with many labels` | QUERY | [7] Matching nodes with many labels |
| 8 | `Match3 - Match fixed length patterns-[8] Matching using relationship predicate with multiples of the same type` | QUERY | [8] Matching using relationship predicate with multiples of the same type |
| 9 | `Match3 - Match fixed length patterns-[9] Get related to related to` | QUERY | [9] Get related to related to |
| 10 | `Match3 - Match fixed length patterns-[10] Matching using self-referencing pattern returns no result` | QUERY | [10] Matching using self-referencing pattern returns no result |
| 11 | `Match3 - Match fixed length patterns-[11] Undirected match in self-relationship graph` | QUERY | [11] Undirected match in self-relationship graph |
| 12 | `Match3 - Match fixed length patterns-[12] Undirected match of self-relationship in self-relationship graph` | QUERY | [12] Undirected match of self-relationship in self-relationship graph |
| 13 | `Match3 - Match fixed length patterns-[13] Directed match on self-relationship graph` | QUERY | [13] Directed match on self-relationship graph |
| 14 | `Match3 - Match fixed length patterns-[14] Directed match of self-relationship on self-relationship graph` | QUERY | [14] Directed match of self-relationship on self-relationship graph |
| 15 | `Match3 - Match fixed length patterns-[15] Mixing directed and undirected pattern parts with self-relationship, simple` | QUERY | [15] Mixing directed and undirected pattern parts with self-relationship, simple |
| 16 | `Match3 - Match fixed length patterns-[16] Mixing directed and undirected pattern parts with self-relationship, undirected` | QUERY | [16] Mixing directed and undirected pattern parts with self-relationship, undire |
| 17 | `Match3 - Match fixed length patterns-[17] Handling cyclic patterns` | QUERY | [17] Handling cyclic patterns |
| 18 | `Match3 - Match fixed length patterns-[18] Handling cyclic patterns when separated into two parts` | QUERY | [18] Handling cyclic patterns when separated into two parts |
| 19 | `Match3 - Match fixed length patterns-[19] Two bound nodes pointing to the same node` | QUERY | [19] Two bound nodes pointing to the same node |
| 20 | `Match3 - Match fixed length patterns-[20] Three bound nodes pointing to the same node` | QUERY | [20] Three bound nodes pointing to the same node |
| 21 | `Match3 - Match fixed length patterns-[21] Three bound nodes pointing to the same node with extra connections` | QUERY | [21] Three bound nodes pointing to the same node with extra connections |
| 22 | `Match3 - Match fixed length patterns-[22] Returning bound nodes that are not part of the pattern` | QUERY | [22] Returning bound nodes that are not part of the pattern |
| 23 | `Match3 - Match fixed length patterns-[23] Matching disconnected patterns` | QUERY | [23] Matching disconnected patterns |
| 24 | `Match3 - Match fixed length patterns-[24] Matching twice with duplicate relationship types on same relationship` | QUERY | [24] Matching twice with duplicate relationship types on same relationship |
| 25 | `Match3 - Match fixed length patterns-[25] Matching twice with an additional node label` | QUERY | [25] Matching twice with an additional node label |
| 26 | `Match3 - Match fixed length patterns-[26] Matching twice with a duplicate predicate` | QUERY | [26] Matching twice with a duplicate predicate |
| 27 | `Match3 - Match fixed length patterns-[27] Matching from null nodes should return no results owing to finding no matches` | QUERY | [27] Matching from null nodes should return no results owing to finding no match |
| 28 | `Match3 - Match fixed length patterns-[28] Matching from null nodes should return no results owing to matches being filtered out` | QUERY | [28] Matching from null nodes should return no results owing to matches being fi |
| 29 | `Match3 - Match fixed length patterns-[29] Fail when re-using a relationship in the same pattern` | ERROR | [29] Fail when re-using a relationship in the same pattern |
| 30 | `Match3 - Match fixed length patterns-[30] Fail when using a list or nodes as a node` | ERROR | [30] Fail when using a list or nodes as a node |

## Match4 (10/10 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match4 - Match variable length patterns scenarios-[1] Handling fixed-length variable length pattern` | QUERY | [1] Handling fixed-length variable length pattern |
| 2 | `Match4 - Match variable length patterns scenarios-[2] Simple variable length pattern` | QUERY | [2] Simple variable length pattern |
| 3 | `Match4 - Match variable length patterns scenarios-[3] Zero-length variable length pattern in the middle of the pattern` | QUERY | [3] Zero-length variable length pattern in the middle of the pattern |
| 4 | `Match4 - Match variable length patterns scenarios-[4] Matching longer variable length paths` | QUERY | [4] Matching longer variable length paths |
| 5 | `Match4 - Match variable length patterns scenarios-[5] Matching variable length pattern with property predicate` | QUERY | [5] Matching variable length pattern with property predicate |
| 6 | `Match4 - Match variable length patterns scenarios-[6] Matching variable length patterns from a bound node` | QUERY | [6] Matching variable length patterns from a bound node |
| 7 | `Match4 - Match variable length patterns scenarios-[7] Matching variable length patterns including a bound relationship` | QUERY | [7] Matching variable length patterns including a bound relationship |
| 8 | `Match4 - Match variable length patterns scenarios-[8] Matching relationships into a list and matching variable length using the list` | QUERY | [8] Matching relationships into a list and matching variable length using the li |
| 9 | `Match4 - Match variable length patterns scenarios-[9] Fail when asterisk operator is missing` | ERROR | [9] Fail when asterisk operator is missing |
| 10 | `Match4 - Match variable length patterns scenarios-[10] Fail on negative bound` | ERROR | [10] Fail on negative bound |

## Match5 (29/29 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match5 - Match variable length patterns over given graphs scenarios-[1] Handling unbounded variable length match` | QUERY | [1] Handling unbounded variable length match |
| 2 | `Match5 - Match variable length patterns over given graphs scenarios-[2] Handling explicitly unbounded variable length match` | QUERY | [2] Handling explicitly unbounded variable length match |
| 3 | `Match5 - Match variable length patterns over given graphs scenarios-[3] Handling single bounded variable length match 1` | QUERY | [3] Handling single bounded variable length match 1 |
| 4 | `Match5 - Match variable length patterns over given graphs scenarios-[4] Handling single bounded variable length match 2` | QUERY | [4] Handling single bounded variable length match 2 |
| 5 | `Match5 - Match variable length patterns over given graphs scenarios-[5] Handling single bounded variable length match 3` | QUERY | [5] Handling single bounded variable length match 3 |
| 6 | `Match5 - Match variable length patterns over given graphs scenarios-[6] Handling upper and lower bounded variable length match 1` | QUERY | [6] Handling upper and lower bounded variable length match 1 |
| 7 | `Match5 - Match variable length patterns over given graphs scenarios-[7] Handling upper and lower bounded variable length match 2` | QUERY | [7] Handling upper and lower bounded variable length match 2 |
| 8 | `Match5 - Match variable length patterns over given graphs scenarios-[8] Handling symmetrically bounded variable length match, bounds are zero` | QUERY | [8] Handling symmetrically bounded variable length match, bounds are zero |
| 9 | `Match5 - Match variable length patterns over given graphs scenarios-[9] Handling symmetrically bounded variable length match, bounds are one` | QUERY | [9] Handling symmetrically bounded variable length match, bounds are one |
| 10 | `Match5 - Match variable length patterns over given graphs scenarios-[10] Handling symmetrically bounded variable length match, bounds are two` | QUERY | [10] Handling symmetrically bounded variable length match, bounds are two |
| 11 | `Match5 - Match variable length patterns over given graphs scenarios-[11] Handling upper and lower bounded variable length match, empty interval 1` | QUERY | [11] Handling upper and lower bounded variable length match, empty interval 1 |
| 12 | `Match5 - Match variable length patterns over given graphs scenarios-[12] Handling upper and lower bounded variable length match, empty interval 2` | QUERY | [12] Handling upper and lower bounded variable length match, empty interval 2 |
| 13 | `Match5 - Match variable length patterns over given graphs scenarios-[13] Handling upper bounded variable length match, empty interval` | QUERY | [13] Handling upper bounded variable length match, empty interval |
| 14 | `Match5 - Match variable length patterns over given graphs scenarios-[14] Handling upper bounded variable length match 1` | QUERY | [14] Handling upper bounded variable length match 1 |
| 15 | `Match5 - Match variable length patterns over given graphs scenarios-[15] Handling upper bounded variable length match 2` | QUERY | [15] Handling upper bounded variable length match 2 |
| 16 | `Match5 - Match variable length patterns over given graphs scenarios-[16] Handling lower bounded variable length match 1` | QUERY | [16] Handling lower bounded variable length match 1 |
| 17 | `Match5 - Match variable length patterns over given graphs scenarios-[17] Handling lower bounded variable length match 2` | QUERY | [17] Handling lower bounded variable length match 2 |
| 18 | `Match5 - Match variable length patterns over given graphs scenarios-[18] Handling lower bounded variable length match 3` | QUERY | [18] Handling lower bounded variable length match 3 |
| 19 | `Match5 - Match variable length patterns over given graphs scenarios-[19] Handling a variable length relationship and a standard relationship in chain, zero length 1` | QUERY | [19] Handling a variable length relationship and a standard relationship in chai |
| 20 | `Match5 - Match variable length patterns over given graphs scenarios-[20] Handling a variable length relationship and a standard relationship in chain, zero length 2` | QUERY | [20] Handling a variable length relationship and a standard relationship in chai |
| 21 | `Match5 - Match variable length patterns over given graphs scenarios-[21] Handling a variable length relationship and a standard relationship in chain, single length 1` | QUERY | [21] Handling a variable length relationship and a standard relationship in chai |
| 22 | `Match5 - Match variable length patterns over given graphs scenarios-[22] Handling a variable length relationship and a standard relationship in chain, single length 2` | QUERY | [22] Handling a variable length relationship and a standard relationship in chai |
| 23 | `Match5 - Match variable length patterns over given graphs scenarios-[23] Handling a variable length relationship and a standard relationship in chain, longer 1` | QUERY | [23] Handling a variable length relationship and a standard relationship in chai |
| 24 | `Match5 - Match variable length patterns over given graphs scenarios-[24] Handling a variable length relationship and a standard relationship in chain, longer 2` | QUERY | [24] Handling a variable length relationship and a standard relationship in chai |
| 25 | `Match5 - Match variable length patterns over given graphs scenarios-[25] Handling a variable length relationship and a standard relationship in chain, longer 3` | QUERY | [25] Handling a variable length relationship and a standard relationship in chai |
| 26 | `Match5 - Match variable length patterns over given graphs scenarios-[26] Handling mixed relationship patterns and directions 1` | QUERY | [26] Handling mixed relationship patterns and directions 1 |
| 27 | `Match5 - Match variable length patterns over given graphs scenarios-[27] Handling mixed relationship patterns and directions 2` | QUERY | [27] Handling mixed relationship patterns and directions 2 |
| 28 | `Match5 - Match variable length patterns over given graphs scenarios-[28] Handling mixed relationship patterns 1` | QUERY | [28] Handling mixed relationship patterns 1 |
| 29 | `Match5 - Match variable length patterns over given graphs scenarios-[29] Handling mixed relationship patterns 2` | QUERY | [29] Handling mixed relationship patterns 2 |

## Match6 (97/97 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match6 - Match named paths scenarios-[1] Zero-length named path` | QUERY | [1] Zero-length named path |
| 2 | `Match6 - Match named paths scenarios-[2] Return a simple path` | QUERY | [2] Return a simple path |
| 3 | `Match6 - Match named paths scenarios-[3] Return a three node path` | QUERY | [3] Return a three node path |
| 4 | `Match6 - Match named paths scenarios-[4] Respecting direction when matching non-existent path` | QUERY | [4] Respecting direction when matching non-existent path |
| 5 | `Match6 - Match named paths scenarios-[5] Path query should return results in written order` | QUERY | [5] Path query should return results in written order |
| 6 | `Match6 - Match named paths scenarios-[6] Handling direction of named paths` | QUERY | [6] Handling direction of named paths |
| 7 | `Match6 - Match named paths scenarios-[7] Respecting direction when matching existing path` | QUERY | [7] Respecting direction when matching existing path |
| 8 | `Match6 - Match named paths scenarios-[8] Respecting direction when matching non-existent path with multiple directions` | QUERY | [8] Respecting direction when matching non-existent path with multiple direction |
| 9 | `Match6 - Match named paths scenarios-[9] Longer path query should return results in written order` | QUERY | [9] Longer path query should return results in written order |
| 10 | `Match6 - Match named paths scenarios-[10] Named path with alternating directed/undirected relationships` | QUERY | [10] Named path with alternating directed/undirected relationships |
| 11 | `Match6 - Match named paths scenarios-[11] Named path with multiple alternating directed/undirected relationships` | QUERY | [11] Named path with multiple alternating directed/undirected relationships |
| 12 | `Match6 - Match named paths scenarios-[12] Matching path with multiple bidirectional relationships` | QUERY | [12] Matching path with multiple bidirectional relationships |
| 13 | `Match6 - Match named paths scenarios-[13] Matching path with both directions should respect other directions` | QUERY | [13] Matching path with both directions should respect other directions |
| 14 | `Match6 - Match named paths scenarios-[14] Named path with undirected fixed variable length pattern` | QUERY | [14] Named path with undirected fixed variable length pattern |
| 15 | `Match6 - Match named paths scenarios-[15] Variable-length named path` | QUERY | [15] Variable-length named path |
| 16 | `Match6 - Match named paths scenarios-[16] Return a var length path` | QUERY | [16] Return a var length path |
| 17 | `Match6 - Match named paths scenarios-[17] Return a named var length path of length zero` | QUERY | [17] Return a named var length path of length zero |
| 18 | `Match6 - Match named paths scenarios-[18] Undirected named path` | QUERY | [18] Undirected named path |
| 19 | `Match6 - Match named paths scenarios-[19] Variable length relationship without lower bound` | QUERY | [19] Variable length relationship without lower bound |
| 20 | `Match6 - Match named paths scenarios-[20] Variable length relationship without bounds` | QUERY | [20] Variable length relationship without bounds |
| 21 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[]-()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[] |
| 22 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[]->()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[] |
| 23 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)<-[]-()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)<-[ |
| 24 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(p)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]- |
| 25 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]->(p)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]- |
| 26 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()<-[]-(p)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()<-[] |
| 27 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[]-(), ()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[] |
| 28 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(p), ()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]- |
| 29 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[]-()-[]-()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(p)-[] |
| 30 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-(p)-[]-()]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]- |
| 31 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]-()-[]-(p)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=()-[]- |
| 32 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r]-(p)-[]->(b), (t), (t)-[*]-(b)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r |
| 33 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r*]-(s)-[]-(b), (p), (t)-[]-(b)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r |
| 34 | `Match6 - Match named paths scenarios-[21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r]-(p)<-[*]-(b), (t), (t)-[]-(b)]` | ERROR | [21] Fail when a node has the same variable in a preceding MATCH [pattern=(a)-[r |
| 35 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 36 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p]->()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 37 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()<-[p]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 38 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p*]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 39 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p*]->()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 40 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()<-[p*]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 41 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p]-(), ()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 42 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p*]-(), ()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 43 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p]-()-[]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 44 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[p*]-()-[]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 45 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[p]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 46 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=()-[]-()-[p*]-()]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 47 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=(a)-[r]-()-[]->(b), (t), (t)-[p*]-(b)]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 48 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=(a)-[r*]-(s)-[p]-(b), (t), (t)-[]-(b)]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 49 | `Match6 - Match named paths scenarios-[22] Fail when a relationship has the same variable in a preceding MATCH [pattern=(a)-[r]-(s)<-[p]-(b), (t), (t)-[]-(b)]` | ERROR | [22] Fail when a relationship has the same variable in a preceding MATCH [patter |
| 50 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = (p)-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = (p) |
| 51 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = (p)-[]->()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = (p) |
| 52 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = (p)<-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = (p) |
| 53 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = ()-[]-(p)]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = ()- |
| 54 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = ()-[]->(p)]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = ()- |
| 55 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=p = ()<-[]-(p)]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=p = ()< |
| 56 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]-(), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]- |
| 57 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]->(), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]- |
| 58 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(p)<-[]-(), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(p)<-[] |
| 59 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[]-(p), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 60 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[]->(p), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[]-> |
| 61 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()<-[]-(p), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()<-[]- |
| 62 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]-(), (), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]- |
| 63 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[p]-(), (), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[p]- |
| 64 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[]-(p), (), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 65 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]-()-[]-(), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(p)-[]- |
| 66 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[]-(p)-[]-(), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 67 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=()-[]-()-[]-(p), p = ()-[]-()]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=()-[]-( |
| 68 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(a)-[r]-(p)-[]-(b), p = (s)-[]-(t), (t), (t)-[]-(b)]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(a)-[r] |
| 69 | `Match6 - Match named paths scenarios-[23] Fail when a node has the same variable in the same pattern [pattern=(a)-[r]-(p)<-[*]-(b), p = (s)-[]-(t), (t), (t)-[]-(b)]` | ERROR | [23] Fail when a node has the same variable in the same pattern [pattern=(a)-[r] |
| 70 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()-[p]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 71 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()-[p]->()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 72 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()<-[p]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 73 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()-[p*]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 74 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()-[p*]->()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 75 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=p = ()<-[p*]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 76 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 77 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p]->(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 78 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()<-[p]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 79 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p*]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 80 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p*]->(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 81 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()<-[p*]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 82 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p]-(), (), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 83 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p*]-(), (), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 84 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p]-()-[]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 85 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[p*]-()-[]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 86 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[p]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 87 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=()-[]-()-[p*]-(), p = ()-[]-()]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 88 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=(a)-[r]-(s)-[p]-(b), p = (s)-[]-(t), (t), (t)-[]-(b)]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 89 | `Match6 - Match named paths scenarios-[24] Fail when a relationship has the same variable in the same pattern [pattern=(a)-[r]-(s)<-[p*]-(b), p = (s)-[]-(t), (t), (t)-[]-(b)]` | ERROR | [24] Fail when a relationship has the same variable in the same pattern [pattern |
| 90 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid=true]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid=true] |
| 91 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid=123]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid=123] |
| 92 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid=123.4]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid=123.4] |
| 93 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid='foo']` | ERROR | [25] Fail when matching a path variable bound to a value [invalid='foo'] |
| 94 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid=[]]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid=[]] |
| 95 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid=[10]]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid=[10]] |
| 96 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid={x: 1}]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid={x: 1}] |
| 97 | `Match6 - Match named paths scenarios-[25] Fail when matching a path variable bound to a value [invalid={x: []}]` | ERROR | [25] Fail when matching a path variable bound to a value [invalid={x: []}] |

## Match7 (31/31 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match7 - Optional match-[1] Simple OPTIONAL MATCH on empty graph` | QUERY | [1] Simple OPTIONAL MATCH on empty graph |
| 2 | `Match7 - Optional match-[2] OPTIONAL MATCH with previously bound nodes` | QUERY | [2] OPTIONAL MATCH with previously bound nodes |
| 3 | `Match7 - Optional match-[3] OPTIONAL MATCH and bound nodes` | QUERY | [3] OPTIONAL MATCH and bound nodes |
| 4 | `Match7 - Optional match-[4] Optionally matching relationship with bound nodes in reverse direction` | QUERY | [4] Optionally matching relationship with bound nodes in reverse direction |
| 5 | `Match7 - Optional match-[5] Optionally matching relationship with a relationship that is already bound` | QUERY | [5] Optionally matching relationship with a relationship that is already bound |
| 6 | `Match7 - Optional match-[6] Optionally matching relationship with a relationship and node that are both already bound` | QUERY | [6] Optionally matching relationship with a relationship and node that are both  |
| 7 | `Match7 - Optional match-[7] MATCH with OPTIONAL MATCH in longer pattern` | QUERY | [7] MATCH with OPTIONAL MATCH in longer pattern |
| 8 | `Match7 - Optional match-[8] Longer pattern with bound nodes without matches` | QUERY | [8] Longer pattern with bound nodes without matches |
| 9 | `Match7 - Optional match-[9] Longer pattern with bound nodes` | QUERY | [9] Longer pattern with bound nodes |
| 10 | `Match7 - Optional match-[10] Optionally matching from null nodes should return null` | QUERY | [10] Optionally matching from null nodes should return null |
| 11 | `Match7 - Optional match-[11] Return two subgraphs with bound undirected relationship and optional relationship` | QUERY | [11] Return two subgraphs with bound undirected relationship and optional relati |
| 12 | `Match7 - Optional match-[12] Variable length optional relationships` | QUERY | [12] Variable length optional relationships |
| 13 | `Match7 - Optional match-[13] Variable length optional relationships with bound nodes` | QUERY | [13] Variable length optional relationships with bound nodes |
| 14 | `Match7 - Optional match-[14] Variable length optional relationships with length predicates` | QUERY | [14] Variable length optional relationships with length predicates |
| 15 | `Match7 - Optional match-[15] Variable length patterns and nulls` | QUERY | [15] Variable length patterns and nulls |
| 16 | `Match7 - Optional match-[16] Optionally matching named paths - null result` | QUERY | [16] Optionally matching named paths - null result |
| 17 | `Match7 - Optional match-[17] Optionally matching named paths - existing result` | QUERY | [17] Optionally matching named paths - existing result |
| 18 | `Match7 - Optional match-[18] Named paths inside optional matches with node predicates` | QUERY | [18] Named paths inside optional matches with node predicates |
| 19 | `Match7 - Optional match-[19] Optionally matching named paths with single and variable length patterns` | QUERY | [19] Optionally matching named paths with single and variable length patterns |
| 20 | `Match7 - Optional match-[20] Variable length optional relationships with bound nodes, no matches` | QUERY | [20] Variable length optional relationships with bound nodes, no matches |
| 21 | `Match7 - Optional match-[21] Handling optional matches between nulls` | QUERY | [21] Handling optional matches between nulls |
| 22 | `Match7 - Optional match-[22] MATCH after OPTIONAL MATCH` | QUERY | [22] MATCH after OPTIONAL MATCH |
| 23 | `Match7 - Optional match-[23] OPTIONAL MATCH with labels on the optional end node` | QUERY | [23] OPTIONAL MATCH with labels on the optional end node |
| 24 | `Match7 - Optional match-[24] Optionally matching self-loops` | QUERY | [24] Optionally matching self-loops |
| 25 | `Match7 - Optional match-[25] Optionally matching self-loops without matches` | QUERY | [25] Optionally matching self-loops without matches |
| 26 | `Match7 - Optional match-[26] Handling correlated optional matches; first does not match implies second does not match` | QUERY | [26] Handling correlated optional matches; first does not match implies second d |
| 27 | `Match7 - Optional match-[27] Handling optional matches between optionally matched entities` | QUERY | [27] Handling optional matches between optionally matched entities |
| 28 | `Match7 - Optional match-[28] Handling optional matches with inline label predicate` | QUERY | [28] Handling optional matches with inline label predicate |
| 29 | `Match7 - Optional match-[29] Satisfies the open world assumption, relationships between same nodes` | QUERY | [29] Satisfies the open world assumption, relationships between same nodes |
| 30 | `Match7 - Optional match-[30] Satisfies the open world assumption, single relationship` | QUERY | [30] Satisfies the open world assumption, single relationship |
| 31 | `Match7 - Optional match-[31] Satisfies the open world assumption, relationships between different nodes` | QUERY | [31] Satisfies the open world assumption, relationships between different nodes |

## Match8 (3/3 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match8 - Match clause interoperation with other clauses-[1] Pattern independent of bound variables results in cross product` | QUERY | [1] Pattern independent of bound variables results in cross product |
| 2 | `Match8 - Match clause interoperation with other clauses-[2] Counting rows after MATCH, MERGE, OPTIONAL MATCH` | QUERY | [2] Counting rows after MATCH, MERGE, OPTIONAL MATCH |
| 3 | `Match8 - Match clause interoperation with other clauses-[3] Matching and disregarding output, then matching again` | QUERY | [3] Matching and disregarding output, then matching again |

## Match9 (9/9 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Match9 - Match deprecated scenarios-[1] Variable length relationship variables are lists of relationships` | QUERY | [1] Variable length relationship variables are lists of relationships |
| 2 | `Match9 - Match deprecated scenarios-[2] Return relationships by collecting them as a list - directed, one way` | QUERY | [2] Return relationships by collecting them as a list - directed, one way |
| 3 | `Match9 - Match deprecated scenarios-[3] Return relationships by collecting them as a list - undirected, starting from two extremes` | QUERY | [3] Return relationships by collecting them as a list - undirected, starting fro |
| 4 | `Match9 - Match deprecated scenarios-[4] Return relationships by collecting them as a list - undirected, starting from one extreme` | QUERY | [4] Return relationships by collecting them as a list - undirected, starting fro |
| 5 | `Match9 - Match deprecated scenarios-[5] Variable length pattern with label predicate on both sides` | QUERY | [5] Variable length pattern with label predicate on both sides |
| 6 | `Match9 - Match deprecated scenarios-[6] Matching relationships into a list and matching variable length using the list, with bound nodes` | QUERY | [6] Matching relationships into a list and matching variable length using the li |
| 7 | `Match9 - Match deprecated scenarios-[7] Matching relationships into a list and matching variable length using the list, with bound nodes, wrong direction` | QUERY | [7] Matching relationships into a list and matching variable length using the li |
| 8 | `Match9 - Match deprecated scenarios-[8] Variable length relationship in OPTIONAL MATCH` | QUERY | [8] Variable length relationship in OPTIONAL MATCH |
| 9 | `Match9 - Match deprecated scenarios-[9] Optionally matching named paths with variable length patterns` | QUERY | [9] Optionally matching named paths with variable length patterns |

## MatchWhere1 (15/15 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere1 - Filter single variable-[1] Filter node with node label predicate on multi variables with multiple bindings` | QUERY | [1] Filter node with node label predicate on multi variables with multiple bindi |
| 2 | `MatchWhere1 - Filter single variable-[2] Filter node with node label predicate on multi variables without any bindings` | QUERY | [2] Filter node with node label predicate on multi variables without any binding |
| 3 | `MatchWhere1 - Filter single variable-[3] Filter node with property predicate on a single variable with multiple bindings` | QUERY | [3] Filter node with property predicate on a single variable with multiple bindi |
| 4 | `MatchWhere1 - Filter single variable-[4] Filter start node of relationship with property predicate on multi variables with multiple bindings` | QUERY | [4] Filter start node of relationship with property predicate on multi variables |
| 5 | `MatchWhere1 - Filter single variable-[5] Filter end node of relationship with property predicate on multi variables with multiple bindings` | QUERY | [5] Filter end node of relationship with property predicate on multi variables w |
| 6 | `MatchWhere1 - Filter single variable-[6] Filter node with a parameter in a property predicate on multi variables with one binding` | QUERY | [6] Filter node with a parameter in a property predicate on multi variables with |
| 7 | `MatchWhere1 - Filter single variable-[7] Filter relationship with relationship type predicate on multi variables with multiple bindings` | QUERY | [7] Filter relationship with relationship type predicate on multi variables with |
| 8 | `MatchWhere1 - Filter single variable-[8] Filter relationship with property predicate on multi variables with multiple bindings` | QUERY | [8] Filter relationship with property predicate on multi variables with multiple |
| 9 | `MatchWhere1 - Filter single variable-[9] Filter relationship with a parameter in a property predicate on multi variables with one binding` | QUERY | [9] Filter relationship with a parameter in a property predicate on multi variab |
| 10 | `MatchWhere1 - Filter single variable-[10] Filter node with disjunctive property predicate on single variables with multiple bindings` | QUERY | [10] Filter node with disjunctive property predicate on single variables with mu |
| 11 | `MatchWhere1 - Filter single variable-[11] Filter relationship with disjunctive relationship type predicate on multi variables with multiple bindings` | QUERY | [11] Filter relationship with disjunctive relationship type predicate on multi v |
| 12 | `MatchWhere1 - Filter single variable-[12] Filter path with path length predicate on multi variables with one binding` | QUERY | [12] Filter path with path length predicate on multi variables with one binding |
| 13 | `MatchWhere1 - Filter single variable-[13] Filter path with false path length predicate on multi variables with one binding` | QUERY | [13] Filter path with false path length predicate on multi variables with one bi |
| 14 | `MatchWhere1 - Filter single variable-[14] Fail when filtering path with property predicate` | ERROR | [14] Fail when filtering path with property predicate |
| 15 | `MatchWhere1 - Filter single variable-[15] Fail on aggregation in WHERE` | ERROR | [15] Fail on aggregation in WHERE |

## MatchWhere2 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere2 - Filter multiple variables-[1] Filter nodes with conjunctive two-part property predicate on multi variables with multiple bindings` | QUERY | [1] Filter nodes with conjunctive two-part property predicate on multi variables |
| 2 | `MatchWhere2 - Filter multiple variables-[2] Filter node with conjunctive multi-part property predicates on multi variables with multiple bindings` | QUERY | [2] Filter node with conjunctive multi-part property predicates on multi variabl |

## MatchWhere3 (3/3 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere3 - Equi-Joins on variables-[1] Join between node identities` | QUERY | [1] Join between node identities |
| 2 | `MatchWhere3 - Equi-Joins on variables-[2] Join between node properties of disconnected nodes` | QUERY | [2] Join between node properties of disconnected nodes |
| 3 | `MatchWhere3 - Equi-Joins on variables-[3] Join between node properties of adjacent nodes` | QUERY | [3] Join between node properties of adjacent nodes |

## MatchWhere4 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere4 - Non-Equi-Joins on variables-[1] Join nodes on inequality` | QUERY | [1] Join nodes on inequality |
| 2 | `MatchWhere4 - Non-Equi-Joins on variables-[2] Join with disjunctive multi-part predicates including patterns` | QUERY | [2] Join with disjunctive multi-part predicates including patterns |

## MatchWhere5 (4/4 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere5 - Filter on predicate resulting in null-[1] Filter out on null` | QUERY | [1] Filter out on null |
| 2 | `MatchWhere5 - Filter on predicate resulting in null-[2] Filter out on null if the AND'd predicate evaluates to false` | QUERY | [2] Filter out on null if the AND'd predicate evaluates to false |
| 3 | `MatchWhere5 - Filter on predicate resulting in null-[3] Filter out on null if the AND'd predicate evaluates to true` | QUERY | [3] Filter out on null if the AND'd predicate evaluates to true |
| 4 | `MatchWhere5 - Filter on predicate resulting in null-[4] Do not filter out on null if the OR'd predicate evaluates to true` | QUERY | [4] Do not filter out on null if the OR'd predicate evaluates to true |

## MatchWhere6 (8/8 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `MatchWhere6 - Filter optional matches-[1] Filter node with node label predicate on multi variables with multiple bindings after MATCH and OPTIONAL MATCH` | QUERY | [1] Filter node with node label predicate on multi variables with multiple bindi |
| 2 | `MatchWhere6 - Filter optional matches-[2] Filter node with false node label predicate after OPTIONAL MATCH` | QUERY | [2] Filter node with false node label predicate after OPTIONAL MATCH |
| 3 | `MatchWhere6 - Filter optional matches-[3] Filter node with property predicate on multi variables with multiple bindings after OPTIONAL MATCH` | QUERY | [3] Filter node with property predicate on multi variables with multiple binding |
| 4 | `MatchWhere6 - Filter optional matches-[4] Do not fail when predicates on optionally matched and missed nodes are invalid` | QUERY | [4] Do not fail when predicates on optionally matched and missed nodes are inval |
| 5 | `MatchWhere6 - Filter optional matches-[5] Matching and optionally matching with unbound nodes and equality predicate in reverse direction` | QUERY | [5] Matching and optionally matching with unbound nodes and equality predicate i |
| 6 | `MatchWhere6 - Filter optional matches-[6] Join nodes on non-equality of properties – OPTIONAL MATCH and WHERE` | QUERY | [6] Join nodes on non-equality of properties – OPTIONAL MATCH and WHERE |
| 7 | `MatchWhere6 - Filter optional matches-[7] Join nodes on non-equality of properties – OPTIONAL MATCH on two relationships and WHERE` | QUERY | [7] Join nodes on non-equality of properties – OPTIONAL MATCH on two relationshi |
| 8 | `MatchWhere6 - Filter optional matches-[8] Join nodes on non-equality of properties – Two OPTIONAL MATCH clauses and WHERE` | QUERY | [8] Join nodes on non-equality of properties – Two OPTIONAL MATCH clauses and WH |

## Return1 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return1 - Return single variable (correct return of values according to their type)-[1] Returning a list property` | QUERY | [1] Returning a list property |
| 2 | `Return1 - Return single variable (correct return of values according to their type)-[2] Fail when returning an undefined variable` | ERROR | [2] Fail when returning an undefined variable |

## Return2 (18/18 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return2 - Return single expression (correctly projecting an expression)-[1] Arithmetic expressions should propagate null values` | QUERY | [1] Arithmetic expressions should propagate null values |
| 2 | `Return2 - Return single expression (correctly projecting an expression)-[2] Returning a node property value` | QUERY | [2] Returning a node property value |
| 3 | `Return2 - Return single expression (correctly projecting an expression)-[3] Missing node property should become null` | QUERY | [3] Missing node property should become null |
| 4 | `Return2 - Return single expression (correctly projecting an expression)-[4] Returning a relationship property value` | QUERY | [4] Returning a relationship property value |
| 5 | `Return2 - Return single expression (correctly projecting an expression)-[5] Missing relationship property should become null` | QUERY | [5] Missing relationship property should become null |
| 6 | `Return2 - Return single expression (correctly projecting an expression)-[6] Adding a property and a literal in projection` | QUERY | [6] Adding a property and a literal in projection |
| 7 | `Return2 - Return single expression (correctly projecting an expression)-[7] Adding list properties in projection` | QUERY | [7] Adding list properties in projection |
| 8 | `Return2 - Return single expression (correctly projecting an expression)-[8] Returning label predicate expression` | QUERY | [8] Returning label predicate expression |
| 9 | `Return2 - Return single expression (correctly projecting an expression)-[9] Returning a projected map` | QUERY | [9] Returning a projected map |
| 10 | `Return2 - Return single expression (correctly projecting an expression)-[10] Return count aggregation over an empty graph` | QUERY | [10] Return count aggregation over an empty graph |
| 11 | `Return2 - Return single expression (correctly projecting an expression)-[11] RETURN does not lose precision on large integers` | QUERY | [11] RETURN does not lose precision on large integers |
| 12 | `Return2 - Return single expression (correctly projecting an expression)-[12] Projecting a list of nodes and relationships` | QUERY | [12] Projecting a list of nodes and relationships |
| 13 | `Return2 - Return single expression (correctly projecting an expression)-[13] Projecting a map of nodes and relationships` | QUERY | [13] Projecting a map of nodes and relationships |
| 14 | `Return2 - Return single expression (correctly projecting an expression)-[14] Do not fail when returning type of deleted relationships` | QUERY | [14] Do not fail when returning type of deleted relationships |
| 15 | `Return2 - Return single expression (correctly projecting an expression)-[15] Fail when returning properties of deleted nodes` | QUERY | [15] Fail when returning properties of deleted nodes |
| 16 | `Return2 - Return single expression (correctly projecting an expression)-[16] Fail when returning labels of deleted nodes` | QUERY | [16] Fail when returning labels of deleted nodes |
| 17 | `Return2 - Return single expression (correctly projecting an expression)-[17] Fail when returning properties of deleted relationships` | QUERY | [17] Fail when returning properties of deleted relationships |
| 18 | `Return2 - Return single expression (correctly projecting an expression)-[18] Fail on projecting a non-existent function` | ERROR | [18] Fail on projecting a non-existent function |

## Return3 (3/3 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return3 - Return multiple expressions (if column order correct)-[1] Returning multiple expressions` | QUERY | [1] Returning multiple expressions |
| 2 | `Return3 - Return multiple expressions (if column order correct)-[2] Returning multiple node property values` | QUERY | [2] Returning multiple node property values |
| 3 | `Return3 - Return multiple expressions (if column order correct)-[3] Projecting nodes and relationships` | QUERY | [3] Projecting nodes and relationships |

## Return4 (11/11 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return4 - Column renaming-[1] Honour the column name for RETURN items` | QUERY | [1] Honour the column name for RETURN items |
| 2 | `Return4 - Column renaming-[2] Support column renaming` | QUERY | [2] Support column renaming |
| 3 | `Return4 - Column renaming-[3] Aliasing expressions` | QUERY | [3] Aliasing expressions |
| 4 | `Return4 - Column renaming-[4] Keeping used expression 1` | QUERY | [4] Keeping used expression 1 |
| 5 | `Return4 - Column renaming-[5] Keeping used expression 2` | QUERY | [5] Keeping used expression 2 |
| 6 | `Return4 - Column renaming-[6] Keeping used expression 3` | QUERY | [6] Keeping used expression 3 |
| 7 | `Return4 - Column renaming-[7] Keeping used expression 4` | QUERY | [7] Keeping used expression 4 |
| 8 | `Return4 - Column renaming-[8] Support column renaming for aggregations` | QUERY | [8] Support column renaming for aggregations |
| 9 | `Return4 - Column renaming-[9] Handle subexpression in aggregation also occurring as standalone expression with nested aggregation in a literal map` | QUERY | [9] Handle subexpression in aggregation also occurring as standalone expression  |
| 10 | `Return4 - Column renaming-[10] Fail when returning multiple columns with same name` | ERROR | [10] Fail when returning multiple columns with same name |
| 11 | `Return4 - Column renaming-[11] Reusing variable names in RETURN` | QUERY | [11] Reusing variable names in RETURN |

## Return5 (5/5 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return5 - Implicit grouping with distinct-[1] DISTINCT inside aggregation should work with lists in maps` | QUERY | [1] DISTINCT inside aggregation should work with lists in maps |
| 2 | `Return5 - Implicit grouping with distinct-[2] DISTINCT on nullable values` | QUERY | [2] DISTINCT on nullable values |
| 3 | `Return5 - Implicit grouping with distinct-[3] DISTINCT inside aggregation should work with nested lists in maps` | QUERY | [3] DISTINCT inside aggregation should work with nested lists in maps |
| 4 | `Return5 - Implicit grouping with distinct-[4] DISTINCT inside aggregation should work with nested lists of maps in maps` | QUERY | [4] DISTINCT inside aggregation should work with nested lists of maps in maps |
| 5 | `Return5 - Implicit grouping with distinct-[5] Aggregate on list values` | QUERY | [5] Aggregate on list values |

## Return6 (21/21 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return6 - Implicit grouping with aggregates-[1] Return count aggregation over nodes` | QUERY | [1] Return count aggregation over nodes |
| 2 | `Return6 - Implicit grouping with aggregates-[2] Projecting an arithmetic expression with aggregation` | QUERY | [2] Projecting an arithmetic expression with aggregation |
| 3 | `Return6 - Implicit grouping with aggregates-[3] Aggregating by a list property has a correct definition of equality` | QUERY | [3] Aggregating by a list property has a correct definition of equality |
| 4 | `Return6 - Implicit grouping with aggregates-[4] Support multiple divisions in aggregate function` | QUERY | [4] Support multiple divisions in aggregate function |
| 5 | `Return6 - Implicit grouping with aggregates-[5] Aggregates inside normal functions` | QUERY | [5] Aggregates inside normal functions |
| 6 | `Return6 - Implicit grouping with aggregates-[6] Handle aggregates inside non-aggregate expressions` | QUERY | [6] Handle aggregates inside non-aggregate expressions |
| 7 | `Return6 - Implicit grouping with aggregates-[7] Aggregate on property` | QUERY | [7] Aggregate on property |
| 8 | `Return6 - Implicit grouping with aggregates-[8] Handle aggregation on functions` | QUERY | [8] Handle aggregation on functions |
| 9 | `Return6 - Implicit grouping with aggregates-[9] Aggregates with arithmetics` | QUERY | [9] Aggregates with arithmetics |
| 10 | `Return6 - Implicit grouping with aggregates-[10] Multiple aggregates on same variable` | QUERY | [10] Multiple aggregates on same variable |
| 11 | `Return6 - Implicit grouping with aggregates-[11] Counting matches` | QUERY | [11] Counting matches |
| 12 | `Return6 - Implicit grouping with aggregates-[12] Counting matches per group` | QUERY | [12] Counting matches per group |
| 13 | `Return6 - Implicit grouping with aggregates-[13] Returning the minimum length of paths` | QUERY | [13] Returning the minimum length of paths |
| 14 | `Return6 - Implicit grouping with aggregates-[14] Aggregates in aggregates` | ERROR | [14] Aggregates in aggregates |
| 15 | `Return6 - Implicit grouping with aggregates-[15] Using `rand()` in aggregations` | ERROR | [15] Using `rand()` in aggregations |
| 16 | `Return6 - Implicit grouping with aggregates-[16] Aggregation on complex expressions` | QUERY | [16] Aggregation on complex expressions |
| 17 | `Return6 - Implicit grouping with aggregates-[17] Handle constants and parameters inside an expression which contains an aggregation expression` | QUERY | [17] Handle constants and parameters inside an expression which contains an aggr |
| 18 | `Return6 - Implicit grouping with aggregates-[18] Handle returned variables inside an expression which contains an aggregation expression` | QUERY | [18] Handle returned variables inside an expression which contains an aggregatio |
| 19 | `Return6 - Implicit grouping with aggregates-[19] Handle returned property accesses inside an expression which contains an aggregation expression` | QUERY | [19] Handle returned property accesses inside an expression which contains an ag |
| 20 | `Return6 - Implicit grouping with aggregates-[20] Fail if not returned variables are used inside an expression which contains an aggregation expression` | ERROR | [20] Fail if not returned variables are used inside an expression which contains |
| 21 | `Return6 - Implicit grouping with aggregates-[21] Fail if more complex expressions, even if returned, are used inside expression which contains an aggregation expression` | ERROR | [21] Fail if more complex expressions, even if returned, are used inside express |

## Return7 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return7 - Return all variables-[1] Return all variables` | QUERY | [1] Return all variables |
| 2 | `Return7 - Return all variables-[2] Fail when using RETURN * without variables in scope` | ERROR | [2] Fail when using RETURN * without variables in scope |

## Return8 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Return8 - Return clause interoperation with other clauses-[1] Return aggregation after With filtering` | QUERY | [1] Return aggregation after With filtering |

## ReturnOrderBy1 (12/12 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[1] ORDER BY should order booleans in the expected order` | QUERY | [1] ORDER BY should order booleans in the expected order |
| 2 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[2] ORDER BY DESC should order booleans in the expected order` | QUERY | [2] ORDER BY DESC should order booleans in the expected order |
| 3 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[3] ORDER BY should order strings in the expected order` | QUERY | [3] ORDER BY should order strings in the expected order |
| 4 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[4] ORDER BY DESC should order strings in the expected order` | QUERY | [4] ORDER BY DESC should order strings in the expected order |
| 5 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[5] ORDER BY should order ints in the expected order` | QUERY | [5] ORDER BY should order ints in the expected order |
| 6 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[6] ORDER BY DESC should order ints in the expected order` | QUERY | [6] ORDER BY DESC should order ints in the expected order |
| 7 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[7] ORDER BY should order floats in the expected order` | QUERY | [7] ORDER BY should order floats in the expected order |
| 8 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[8] ORDER BY DESC should order floats in the expected order` | QUERY | [8] ORDER BY DESC should order floats in the expected order |
| 9 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[9] ORDER BY should order lists in the expected order` | QUERY | [9] ORDER BY should order lists in the expected order |
| 10 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[10] ORDER BY DESC should order lists in the expected order` | QUERY | [10] ORDER BY DESC should order lists in the expected order |
| 11 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[11] ORDER BY should order distinct types in the expected order` | QUERY | [11] ORDER BY should order distinct types in the expected order |
| 12 | `ReturnOrderBy1 - Order by a single variable (correct order of values according to their type)-[12] ORDER BY DESC should order distinct types in the expected order` | QUERY | [12] ORDER BY DESC should order distinct types in the expected order |

## ReturnOrderBy2 (14/14 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[1] ORDER BY should return results in ascending order` | QUERY | [1] ORDER BY should return results in ascending order |
| 2 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[2] ORDER BY DESC should return results in descending order` | QUERY | [2] ORDER BY DESC should return results in descending order |
| 3 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[3] Sort on aggregated function` | QUERY | [3] Sort on aggregated function |
| 4 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[4] Support sort and distinct` | QUERY | [4] Support sort and distinct |
| 5 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[5] Support ordering by a property after being distinct-ified` | QUERY | [5] Support ordering by a property after being distinct-ified |
| 6 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[6] Count star should count everything in scope` | QUERY | [6] Count star should count everything in scope |
| 7 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[7] Ordering with aggregation` | QUERY | [7] Ordering with aggregation |
| 8 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[8] Returning all variables with ordering` | QUERY | [8] Returning all variables with ordering |
| 9 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[9] Using aliased DISTINCT expression in ORDER BY` | QUERY | [9] Using aliased DISTINCT expression in ORDER BY |
| 10 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[10] Returned columns do not change from using ORDER BY` | QUERY | [10] Returned columns do not change from using ORDER BY |
| 11 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[11] Aggregates ordered by arithmetics` | QUERY | [11] Aggregates ordered by arithmetics |
| 12 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[12] Aggregation of named paths` | QUERY | [12] Aggregation of named paths |
| 13 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[13] Fail when sorting on variable removed by DISTINCT` | ERROR | [13] Fail when sorting on variable removed by DISTINCT |
| 14 | `ReturnOrderBy2 - Order by a single expression (order of projection)-[14] Fail on aggregation in ORDER BY after RETURN` | ERROR | [14] Fail on aggregation in ORDER BY after RETURN |

## ReturnOrderBy3 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy3 - Order by multiple expressions (order obey priority of expressions)-[1] Sort on aggregate function and normal property` | QUERY | [1] Sort on aggregate function and normal property |

## ReturnOrderBy4 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy4 - Order by in combination with projection-[1] ORDER BY of a column introduced in RETURN should return salient results in ascending order` | QUERY | [1] ORDER BY of a column introduced in RETURN should return salient results in a |
| 2 | `ReturnOrderBy4 - Order by in combination with projection-[2] Handle projections with ORDER BY` | QUERY | [2] Handle projections with ORDER BY |

## ReturnOrderBy5 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy5 - Order by in combination with column renaming-[1] Renaming columns before ORDER BY should return results in ascending order` | QUERY | [1] Renaming columns before ORDER BY should return results in ascending order |

## ReturnOrderBy6 (5/5 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnOrderBy6 - Aggregation expressions in order by-[1] Handle constants and parameters inside an order by item which contains an aggregation expression` | QUERY | [1] Handle constants and parameters inside an order by item which contains an ag |
| 2 | `ReturnOrderBy6 - Aggregation expressions in order by-[2] Handle returned aliases inside an order by item which contains an aggregation expression` | QUERY | [2] Handle returned aliases inside an order by item which contains an aggregatio |
| 3 | `ReturnOrderBy6 - Aggregation expressions in order by-[3] Handle returned property accesses inside an order by item which contains an aggregation expression` | QUERY | [3] Handle returned property accesses inside an order by item which contains an  |
| 4 | `ReturnOrderBy6 - Aggregation expressions in order by-[4] Fail if not returned variables are used inside an order by item which contains an aggregation expression` | ERROR | [4] Fail if not returned variables are used inside an order by item which contai |
| 5 | `ReturnOrderBy6 - Aggregation expressions in order by-[5] Fail if more complex expressions, even if returned, are used inside an order by item which contains an aggregation expression` | ERROR | [5] Fail if more complex expressions, even if returned, are used inside an order |

## ReturnSkipLimit1 (11/11 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnSkipLimit1 - Skip-[1] Start the result from the second row` | QUERY | [1] Start the result from the second row |
| 2 | `ReturnSkipLimit1 - Skip-[2] Start the result from the second row by param` | QUERY | [2] Start the result from the second row by param |
| 3 | `ReturnSkipLimit1 - Skip-[3] SKIP with an expression that does not depend on variables` | QUERY | [3] SKIP with an expression that does not depend on variables |
| 4 | `ReturnSkipLimit1 - Skip-[4] Accept skip zero` | QUERY | [4] Accept skip zero |
| 5 | `ReturnSkipLimit1 - Skip-[5] SKIP with an expression that depends on variables should fail` | ERROR | [5] SKIP with an expression that depends on variables should fail |
| 6 | `ReturnSkipLimit1 - Skip-[6] Negative parameter for SKIP should fail` | ERROR | [6] Negative parameter for SKIP should fail |
| 7 | `ReturnSkipLimit1 - Skip-[7] Negative SKIP should fail` | ERROR | [7] Negative SKIP should fail |
| 8 | `ReturnSkipLimit1 - Skip-[8] Floating point parameter for SKIP should fail` | ERROR | [8] Floating point parameter for SKIP should fail |
| 9 | `ReturnSkipLimit1 - Skip-[9] Floating point SKIP should fail` | ERROR | [9] Floating point SKIP should fail |
| 10 | `ReturnSkipLimit1 - Skip-[10] Fail when using non-constants in SKIP` | ERROR | [10] Fail when using non-constants in SKIP |
| 11 | `ReturnSkipLimit1 - Skip-[11] Fail when using negative value in SKIP` | ERROR | [11] Fail when using negative value in SKIP |

## ReturnSkipLimit2 (17/17 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnSkipLimit2 - Limit-[1] Limit to two hits` | QUERY | [1] Limit to two hits |
| 2 | `ReturnSkipLimit2 - Limit-[2] Limit to two hits with explicit order` | QUERY | [2] Limit to two hits with explicit order |
| 3 | `ReturnSkipLimit2 - Limit-[3] LIMIT 0 should return an empty result` | QUERY | [3] LIMIT 0 should return an empty result |
| 4 | `ReturnSkipLimit2 - Limit-[4] Handle ORDER BY with LIMIT 1` | QUERY | [4] Handle ORDER BY with LIMIT 1 |
| 5 | `ReturnSkipLimit2 - Limit-[5] ORDER BY with LIMIT 0 should not generate errors` | QUERY | [5] ORDER BY with LIMIT 0 should not generate errors |
| 6 | `ReturnSkipLimit2 - Limit-[6] LIMIT with an expression that does not depend on variables` | QUERY | [6] LIMIT with an expression that does not depend on variables |
| 7 | `ReturnSkipLimit2 - Limit-[7] Limit to more rows than actual results 1` | QUERY | [7] Limit to more rows than actual results 1 |
| 8 | `ReturnSkipLimit2 - Limit-[8] Limit to more rows than actual results 2` | QUERY | [8] Limit to more rows than actual results 2 |
| 9 | `ReturnSkipLimit2 - Limit-[9] Fail when using non-constants in LIMIT` | ERROR | [9] Fail when using non-constants in LIMIT |
| 10 | `ReturnSkipLimit2 - Limit-[10] Negative parameter for LIMIT should fail` | ERROR | [10] Negative parameter for LIMIT should fail |
| 11 | `ReturnSkipLimit2 - Limit-[11] Negative parameter for LIMIT with ORDER BY should fail` | ERROR | [11] Negative parameter for LIMIT with ORDER BY should fail |
| 12 | `ReturnSkipLimit2 - Limit-[12] Fail when using negative value in LIMIT 1` | ERROR | [12] Fail when using negative value in LIMIT 1 |
| 13 | `ReturnSkipLimit2 - Limit-[13] Fail when using negative value in LIMIT 2` | ERROR | [13] Fail when using negative value in LIMIT 2 |
| 14 | `ReturnSkipLimit2 - Limit-[14] Floating point parameter for LIMIT should fail` | ERROR | [14] Floating point parameter for LIMIT should fail |
| 15 | `ReturnSkipLimit2 - Limit-[15] Floating point parameter for LIMIT with ORDER BY should fail` | ERROR | [15] Floating point parameter for LIMIT with ORDER BY should fail |
| 16 | `ReturnSkipLimit2 - Limit-[16] Fail when using floating point in LIMIT 1` | ERROR | [16] Fail when using floating point in LIMIT 1 |
| 17 | `ReturnSkipLimit2 - Limit-[17] Fail when using floating point in LIMIT 2` | ERROR | [17] Fail when using floating point in LIMIT 2 |

## ReturnSkipLimit3 (3/3 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `ReturnSkipLimit3 - Skip and limit-[1] Get rows in the middle` | QUERY | [1] Get rows in the middle |
| 2 | `ReturnSkipLimit3 - Skip and limit-[2] Get rows in the middle by param` | QUERY | [2] Get rows in the middle by param |
| 3 | `ReturnSkipLimit3 - Skip and limit-[3] Limiting amount of rows when there are fewer left than the LIMIT argument` | QUERY | [3] Limiting amount of rows when there are fewer left than the LIMIT argument |

## Aggregation1 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation1 - Count-[1] Count only non-null values` | QUERY | [1] Count only non-null values |
| 2 | `Aggregation1 - Count-[2] Counting loop relationships` | QUERY | [2] Counting loop relationships |

## Aggregation2 (12/12 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation2 - Min and Max-[1] `max()` over integers` | QUERY | [1] `max()` over integers |
| 2 | `Aggregation2 - Min and Max-[2] `min()` over integers` | QUERY | [2] `min()` over integers |
| 3 | `Aggregation2 - Min and Max-[3] `max()` over floats` | QUERY | [3] `max()` over floats |
| 4 | `Aggregation2 - Min and Max-[4] `min()` over floats` | QUERY | [4] `min()` over floats |
| 5 | `Aggregation2 - Min and Max-[5] `max()` over mixed numeric values` | QUERY | [5] `max()` over mixed numeric values |
| 6 | `Aggregation2 - Min and Max-[6] `min()` over mixed numeric values` | QUERY | [6] `min()` over mixed numeric values |
| 7 | `Aggregation2 - Min and Max-[7] `max()` over strings` | QUERY | [7] `max()` over strings |
| 8 | `Aggregation2 - Min and Max-[8] `min()` over strings` | QUERY | [8] `min()` over strings |
| 9 | `Aggregation2 - Min and Max-[9] `max()` over list values` | QUERY | [9] `max()` over list values |
| 10 | `Aggregation2 - Min and Max-[10] `min()` over list values` | QUERY | [10] `min()` over list values |
| 11 | `Aggregation2 - Min and Max-[11] `max()` over mixed values` | QUERY | [11] `max()` over mixed values |
| 12 | `Aggregation2 - Min and Max-[12] `min()` over mixed values` | QUERY | [12] `min()` over mixed values |

## Aggregation3 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation3 - Sum-[1] Sum only non-null values` | QUERY | [1] Sum only non-null values |
| 2 | `Aggregation3 - Sum-[2] No overflow during summation` | QUERY | [2] No overflow during summation |

## Aggregation5 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation5 - Collect-[1] `collect()` filtering nulls` | QUERY | [1] `collect()` filtering nulls |
| 2 | `Aggregation5 - Collect-[2] OPTIONAL MATCH and `collect()` on node property` | QUERY | [2] OPTIONAL MATCH and `collect()` on node property |

## Aggregation6 (13/13 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation6 - Percentiles-[1] `percentileDisc()` [p=0.0, result=10.0]` | QUERY | [1] `percentileDisc()` [p=0.0, result=10.0] |
| 2 | `Aggregation6 - Percentiles-[1] `percentileDisc()` [p=0.5, result=20.0]` | QUERY | [1] `percentileDisc()` [p=0.5, result=20.0] |
| 3 | `Aggregation6 - Percentiles-[1] `percentileDisc()` [p=1.0, result=30.0]` | QUERY | [1] `percentileDisc()` [p=1.0, result=30.0] |
| 4 | `Aggregation6 - Percentiles-[2] `percentileCont()` [p=0.0, result=10.0]` | QUERY | [2] `percentileCont()` [p=0.0, result=10.0] |
| 5 | `Aggregation6 - Percentiles-[2] `percentileCont()` [p=0.5, result=20.0]` | QUERY | [2] `percentileCont()` [p=0.5, result=20.0] |
| 6 | `Aggregation6 - Percentiles-[2] `percentileCont()` [p=1.0, result=30.0]` | QUERY | [2] `percentileCont()` [p=1.0, result=30.0] |
| 7 | `Aggregation6 - Percentiles-[3] `percentileCont()` failing on bad arguments [percentile=1000]` | QUERY | [3] `percentileCont()` failing on bad arguments [percentile=1000] |
| 8 | `Aggregation6 - Percentiles-[3] `percentileCont()` failing on bad arguments [percentile=-1]` | QUERY | [3] `percentileCont()` failing on bad arguments [percentile=-1] |
| 9 | `Aggregation6 - Percentiles-[3] `percentileCont()` failing on bad arguments [percentile=1.1]` | QUERY | [3] `percentileCont()` failing on bad arguments [percentile=1.1] |
| 10 | `Aggregation6 - Percentiles-[4] `percentileDisc()` failing on bad arguments [percentile=1000]` | QUERY | [4] `percentileDisc()` failing on bad arguments [percentile=1000] |
| 11 | `Aggregation6 - Percentiles-[4] `percentileDisc()` failing on bad arguments [percentile=-1]` | QUERY | [4] `percentileDisc()` failing on bad arguments [percentile=-1] |
| 12 | `Aggregation6 - Percentiles-[4] `percentileDisc()` failing on bad arguments [percentile=1.1]` | QUERY | [4] `percentileDisc()` failing on bad arguments [percentile=1.1] |
| 13 | `Aggregation6 - Percentiles-[5] `percentileDisc()` failing in more involved query` | QUERY | [5] `percentileDisc()` failing in more involved query |

## Aggregation8 (4/4 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Aggregation8 - DISTINCT-[1] Distinct on unbound node` | QUERY | [1] Distinct on unbound node |
| 2 | `Aggregation8 - DISTINCT-[2] Distinct on null` | QUERY | [2] Distinct on null |
| 3 | `Aggregation8 - DISTINCT-[3] Collect distinct nulls` | QUERY | [3] Collect distinct nulls |
| 4 | `Aggregation8 - DISTINCT-[4] Collect distinct values mixed with nulls` | QUERY | [4] Collect distinct values mixed with nulls |

## Boolean1 (30/30 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Boolean1 - And logical operations-[1] Conjunction of two truth values` | QUERY | [1] Conjunction of two truth values |
| 2 | `Boolean1 - And logical operations-[2] Conjunction of three truth values` | QUERY | [2] Conjunction of three truth values |
| 3 | `Boolean1 - And logical operations-[3] Conjunction of many truth values` | QUERY | [3] Conjunction of many truth values |
| 4 | `Boolean1 - And logical operations-[4] Conjunction is commutative on non-null` | QUERY | [4] Conjunction is commutative on non-null |
| 5 | `Boolean1 - And logical operations-[5] Conjunction is commutative on null` | QUERY | [5] Conjunction is commutative on null |
| 6 | `Boolean1 - And logical operations-[6] Conjunction is associative on non-null` | QUERY | [6] Conjunction is associative on non-null |
| 7 | `Boolean1 - And logical operations-[7] Conjunction is associative on null` | QUERY | [7] Conjunction is associative on null |
| 8 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=123, b=true]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=123, b=true] |
| 9 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=123.4, b=false]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=123.4, b=false] |
| 10 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=123.4, b=null]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=123.4, b=null] |
| 11 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a='foo', b=true]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a='foo', b=true] |
| 12 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=[], b=false]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=[], b=false] |
| 13 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=[true], b=false]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=[true], b=false] |
| 14 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=[null], b=null]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=[null], b=null] |
| 15 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a={}, b=true]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a={}, b=true] |
| 16 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a={x: []}, b=true]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a={x: []}, b=true] |
| 17 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=false, b=123]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=false, b=123] |
| 18 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=true, b=123.4]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=true, b=123.4] |
| 19 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=false, b='foo']` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=false, b='foo'] |
| 20 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=null, b='foo']` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=null, b='foo'] |
| 21 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=true, b=[]]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=true, b=[]] |
| 22 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=true, b=[false]]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=true, b=[false]] |
| 23 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=null, b=[null]]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=null, b=[null]] |
| 24 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=false, b={}]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=false, b={}] |
| 25 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=false, b={x: []}]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=false, b={x: []}] |
| 26 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=123, b='foo']` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=123, b='foo'] |
| 27 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=123.4, b=123.4]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=123.4, b=123.4] |
| 28 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a='foo', b={x: []}]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a='foo', b={x: []}] |
| 29 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a=[true], b=[true]]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a=[true], b=[true]] |
| 30 | `Boolean1 - And logical operations-[8] Fail on conjunction of at least one non-booleans [a={x: []}, b=[123]]` | ERROR | [8] Fail on conjunction of at least one non-booleans [a={x: []}, b=[123]] |

## Boolean2 (30/30 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Boolean2 - OR logical operations-[1] Disjunction of two truth values` | QUERY | [1] Disjunction of two truth values |
| 2 | `Boolean2 - OR logical operations-[2] Disjunction of three truth values` | QUERY | [2] Disjunction of three truth values |
| 3 | `Boolean2 - OR logical operations-[3] Disjunction of many truth values` | QUERY | [3] Disjunction of many truth values |
| 4 | `Boolean2 - OR logical operations-[4] Disjunction is commutative on non-null` | QUERY | [4] Disjunction is commutative on non-null |
| 5 | `Boolean2 - OR logical operations-[5] Disjunction is commutative on null` | QUERY | [5] Disjunction is commutative on null |
| 6 | `Boolean2 - OR logical operations-[6] Disjunction is associative on non-null` | QUERY | [6] Disjunction is associative on non-null |
| 7 | `Boolean2 - OR logical operations-[7] Disjunction is associative on null` | QUERY | [7] Disjunction is associative on null |
| 8 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=123, b=true]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=123, b=true] |
| 9 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=123.4, b=false]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=123.4, b=false] |
| 10 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=123.4, b=null]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=123.4, b=null] |
| 11 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a='foo', b=true]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a='foo', b=true] |
| 12 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=[], b=false]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=[], b=false] |
| 13 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=[true], b=false]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=[true], b=false] |
| 14 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=[null], b=null]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=[null], b=null] |
| 15 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a={}, b=true]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a={}, b=true] |
| 16 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a={x: []}, b=true]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a={x: []}, b=true] |
| 17 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=false, b=123]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=false, b=123] |
| 18 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=true, b=123.4]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=true, b=123.4] |
| 19 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=false, b='foo']` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=false, b='foo'] |
| 20 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=null, b='foo']` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=null, b='foo'] |
| 21 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=true, b=[]]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=true, b=[]] |
| 22 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=true, b=[false]]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=true, b=[false]] |
| 23 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=null, b=[null]]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=null, b=[null]] |
| 24 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=false, b={}]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=false, b={}] |
| 25 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=false, b={x: []}]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=false, b={x: []}] |
| 26 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=123, b='foo']` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=123, b='foo'] |
| 27 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=123.4, b=123.4]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=123.4, b=123.4] |
| 28 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a='foo', b={x: []}]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a='foo', b={x: []}] |
| 29 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a=[true], b=[true]]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a=[true], b=[true]] |
| 30 | `Boolean2 - OR logical operations-[8] Fail on disjunction of at least one non-booleans [a={x: []}, b=[123]]` | ERROR | [8] Fail on disjunction of at least one non-booleans [a={x: []}, b=[123]] |

## Boolean3 (30/30 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Boolean3 - XOR logical operations-[1] Exclusive disjunction of two truth values` | QUERY | [1] Exclusive disjunction of two truth values |
| 2 | `Boolean3 - XOR logical operations-[2] Exclusive disjunction of three truth values` | QUERY | [2] Exclusive disjunction of three truth values |
| 3 | `Boolean3 - XOR logical operations-[3] Exclusive disjunction of many truth values` | QUERY | [3] Exclusive disjunction of many truth values |
| 4 | `Boolean3 - XOR logical operations-[4] Exclusive disjunction is commutative on non-null` | QUERY | [4] Exclusive disjunction is commutative on non-null |
| 5 | `Boolean3 - XOR logical operations-[5] Exclusive disjunction is commutative on null` | QUERY | [5] Exclusive disjunction is commutative on null |
| 6 | `Boolean3 - XOR logical operations-[6] Exclusive disjunction is associative on non-null` | QUERY | [6] Exclusive disjunction is associative on non-null |
| 7 | `Boolean3 - XOR logical operations-[7] Exclusive disjunction is associative on null` | QUERY | [7] Exclusive disjunction is associative on null |
| 8 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=123, b=true]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=123, b=true] |
| 9 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=false]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=false |
| 10 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=null]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=null] |
| 11 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a='foo', b=true]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a='foo', b=true] |
| 12 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=[], b=false]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=[], b=false] |
| 13 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=[true], b=false]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=[true], b=fals |
| 14 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=[null], b=null]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=[null], b=null |
| 15 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a={}, b=true]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a={}, b=true] |
| 16 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a={x: []}, b=true]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a={x: []}, b=tru |
| 17 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=false, b=123]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=false, b=123] |
| 18 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=123.4]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=123.4] |
| 19 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=false, b='foo']` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=false, b='foo' |
| 20 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=null, b='foo']` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=null, b='foo'] |
| 21 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=[]]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=[]] |
| 22 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=[false]]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=true, b=[false |
| 23 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=null, b=[null]]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=null, b=[null] |
| 24 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=false, b={}]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=false, b={}] |
| 25 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=false, b={x: []}]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=false, b={x: [ |
| 26 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=123, b='foo']` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=123, b='foo'] |
| 27 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=123.4]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=123.4, b=123.4 |
| 28 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a='foo', b={x: []}]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a='foo', b={x: [ |
| 29 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a=[true], b=[true]]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a=[true], b=[tru |
| 30 | `Boolean3 - XOR logical operations-[8] Fail on exclusive disjunction of at least one non-booleans [a={x: []}, b=[123]]` | ERROR | [8] Fail on exclusive disjunction of at least one non-booleans [a={x: []}, b=[12 |

## Boolean4 (52/52 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Boolean4 - NOT logical operations-[1] Logical negation of truth values` | QUERY | [1] Logical negation of truth values |
| 2 | `Boolean4 - NOT logical operations-[2] Double logical negation of truth values` | QUERY | [2] Double logical negation of truth values |
| 3 | `Boolean4 - NOT logical operations-[3] NOT and false` | QUERY | [3] NOT and false |
| 4 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=0]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=0] |
| 5 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=1]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=1] |
| 6 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=123]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=123] |
| 7 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=123.4]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=123.4] |
| 8 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal='']` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=''] |
| 9 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal='false']` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal='false'] |
| 10 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal='true']` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal='true'] |
| 11 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal='foo']` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal='foo'] |
| 12 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[]] |
| 13 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[null]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[null]] |
| 14 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[true]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[true]] |
| 15 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[false]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[false]] |
| 16 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[true, false]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[true, false]] |
| 17 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[false, true]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[false, true]] |
| 18 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[0]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[0]] |
| 19 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[1]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[1]] |
| 20 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[1, 2, 3]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[1, 2, 3]] |
| 21 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[0.0]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[0.0]] |
| 22 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[1.0]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[1.0]] |
| 23 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=[1.0, 2.1]]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=[1.0, 2.1]] |
| 24 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=['']]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=['']] |
| 25 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=['', '']]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=['', '']] |
| 26 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=['true']]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=['true']] |
| 27 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=['false']]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=['false']] |
| 28 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal=['a', 'b']]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal=['a', 'b']] |
| 29 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={}] |
| 30 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: null}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: null}] |
| 31 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: null}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: null}] |
| 32 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: true}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: true}] |
| 33 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: false}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: false}] |
| 34 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={true: true}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={true: true}] |
| 35 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={false: false}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={false: false}] |
| 36 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={bool: true}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={bool: true}] |
| 37 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={bool: false}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={bool: false}] |
| 38 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: 0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: 0}] |
| 39 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: 1}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: 1}] |
| 40 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 0}] |
| 41 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 1}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 1}] |
| 42 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 1, b: 2}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 1, b: 2}] |
| 43 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: 0.0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: 0.0}] |
| 44 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: 1.0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: 1.0}] |
| 45 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 0.0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 0.0}] |
| 46 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 1.0}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 1.0}] |
| 47 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 1.0, b: 2.1}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 1.0, b: 2.1}] |
| 48 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={``: ''}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={``: ''}] |
| 49 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: ''}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: ''}] |
| 50 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 'a'}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 'a'}] |
| 51 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 'a', b: 'b'}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 'a', b: 'b'}] |
| 52 | `Boolean4 - NOT logical operations-[4] Fail when using NOT on a non-boolean literal [literal={a: 12, b: true}]` | ERROR | [4] Fail when using NOT on a non-boolean literal [literal={a: 12, b: true}] |

## Boolean5 (8/8 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Boolean5 - Interop of logical operations-[1] Disjunction is distributive over conjunction on non-null` | QUERY | [1] Disjunction is distributive over conjunction on non-null |
| 2 | `Boolean5 - Interop of logical operations-[2] Disjunction is distributive over conjunction on null` | QUERY | [2] Disjunction is distributive over conjunction on null |
| 3 | `Boolean5 - Interop of logical operations-[3] Conjunction is distributive over disjunction on non-null` | QUERY | [3] Conjunction is distributive over disjunction on non-null |
| 4 | `Boolean5 - Interop of logical operations-[4] Conjunction is distributive over disjunction on null` | QUERY | [4] Conjunction is distributive over disjunction on null |
| 5 | `Boolean5 - Interop of logical operations-[5] Conjunction is distributive over exclusive disjunction on non-null` | QUERY | [5] Conjunction is distributive over exclusive disjunction on non-null |
| 6 | `Boolean5 - Interop of logical operations-[6] Conjunction is not distributive over exclusive disjunction on null` | QUERY | [6] Conjunction is not distributive over exclusive disjunction on null |
| 7 | `Boolean5 - Interop of logical operations-[7] De Morgan's law on non-null: the negation of a disjunction is the conjunction of the negations` | QUERY | [7] De Morgan's law on non-null: the negation of a disjunction is the conjunctio |
| 8 | `Boolean5 - Interop of logical operations-[8] De Morgan's law on non-null: the negation of a conjunction is the disjunction of the negations` | QUERY | [8] De Morgan's law on non-null: the negation of a conjunction is the disjunctio |

## Comparison1 (43/43 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Comparison1 - Equality-[1] Number-typed integer comparison` | QUERY | [1] Number-typed integer comparison |
| 2 | `Comparison1 - Equality-[2] Number-typed float comparison` | QUERY | [2] Number-typed float comparison |
| 3 | `Comparison1 - Equality-[3] Any-typed string comparison` | QUERY | [3] Any-typed string comparison |
| 4 | `Comparison1 - Equality-[4] Comparing nodes to nodes` | QUERY | [4] Comparing nodes to nodes |
| 5 | `Comparison1 - Equality-[5] Comparing relationships to relationships` | QUERY | [5] Comparing relationships to relationships |
| 6 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=[1, 2], rhs=[1], result=false]` | QUERY | [6] Comparing lists to lists [lhs=[1, 2], rhs=[1], result=false] |
| 7 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=[null], rhs=[1], result=null]` | QUERY | [6] Comparing lists to lists [lhs=[null], rhs=[1], result=null] |
| 8 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=['a'], rhs=[1], result=false]` | QUERY | [6] Comparing lists to lists [lhs=['a'], rhs=[1], result=false] |
| 9 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=[[1]], rhs=[[1], [null]], result=false]` | QUERY | [6] Comparing lists to lists [lhs=[[1]], rhs=[[1], [null]], result=false] |
| 10 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=[[1], [2]], rhs=[[1], [null]], result=null]` | QUERY | [6] Comparing lists to lists [lhs=[[1], [2]], rhs=[[1], [null]], result=null] |
| 11 | `Comparison1 - Equality-[6] Comparing lists to lists [lhs=[[1], [2, 3]], rhs=[[1], [null]], result=false]` | QUERY | [6] Comparing lists to lists [lhs=[[1], [2, 3]], rhs=[[1], [null]], result=false |
| 12 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={}, rhs={}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={}, rhs={}, result=true] |
| 13 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: true}, rhs={k: true}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={k: true}, rhs={k: true}, result=true] |
| 14 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1}, rhs={k: 1}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={k: 1}, rhs={k: 1}, result=true] |
| 15 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1.0}, rhs={k: 1.0}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={k: 1.0}, rhs={k: 1.0}, result=true] |
| 16 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 'abc'}, rhs={k: 'abc'}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={k: 'abc'}, rhs={k: 'abc'}, result=true] |
| 17 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 'a', l: 2}, rhs={k: 'a', l: 2}, result=true]` | QUERY | [7] Comparing maps to maps [lhs={k: 'a', l: 2}, rhs={k: 'a', l: 2}, result=true] |
| 18 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={}, rhs={k: null}, result=false]` | QUERY | [7] Comparing maps to maps [lhs={}, rhs={k: null}, result=false] |
| 19 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: null}, rhs={}, result=false]` | QUERY | [7] Comparing maps to maps [lhs={k: null}, rhs={}, result=false] |
| 20 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1}, rhs={k: 1, l: null}, result=false]` | QUERY | [7] Comparing maps to maps [lhs={k: 1}, rhs={k: 1, l: null}, result=false] |
| 21 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: null, l: 1}, rhs={l: 1}, result=false]` | QUERY | [7] Comparing maps to maps [lhs={k: null, l: 1}, rhs={l: 1}, result=false] |
| 22 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: null}, rhs={k: null, l: null}, result=false]` | QUERY | [7] Comparing maps to maps [lhs={k: null}, rhs={k: null, l: null}, result=false] |
| 23 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: null}, rhs={k: null}, result=null]` | QUERY | [7] Comparing maps to maps [lhs={k: null}, rhs={k: null}, result=null] |
| 24 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1}, rhs={k: null}, result=null]` | QUERY | [7] Comparing maps to maps [lhs={k: 1}, rhs={k: null}, result=null] |
| 25 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: null, l: null}, result=null]` | QUERY | [7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: null, l: null}, result= |
| 26 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: null, l: 1}, result=null]` | QUERY | [7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: null, l: 1}, result=nul |
| 27 | `Comparison1 - Equality-[7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: 1, l: 1}, result=null]` | QUERY | [7] Comparing maps to maps [lhs={k: 1, l: null}, rhs={k: 1, l: 1}, result=null] |
| 28 | `Comparison1 - Equality-[8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=1]` | QUERY | [8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=1] |
| 29 | `Comparison1 - Equality-[8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=1.0]` | QUERY | [8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=1.0] |
| 30 | `Comparison1 - Equality-[8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=0.0 / 0.0]` | QUERY | [8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs=0.0 / 0.0] |
| 31 | `Comparison1 - Equality-[8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs='a']` | QUERY | [8] Equality and inequality of NaN [lhs=0.0 / 0.0, rhs='a'] |
| 32 | `Comparison1 - Equality-[9] Equality between strings and numbers [lhs=1.0, rhs=1.0, result=true]` | QUERY | [9] Equality between strings and numbers [lhs=1.0, rhs=1.0, result=true] |
| 33 | `Comparison1 - Equality-[9] Equality between strings and numbers [lhs=1, rhs=1.0, result=true]` | QUERY | [9] Equality between strings and numbers [lhs=1, rhs=1.0, result=true] |
| 34 | `Comparison1 - Equality-[9] Equality between strings and numbers [lhs='1.0', rhs=1.0, result=false]` | QUERY | [9] Equality between strings and numbers [lhs='1.0', rhs=1.0, result=false] |
| 35 | `Comparison1 - Equality-[9] Equality between strings and numbers [lhs='1', rhs=1, result=false]` | QUERY | [9] Equality between strings and numbers [lhs='1', rhs=1, result=false] |
| 36 | `Comparison1 - Equality-[10] Handling inlined equality of large integer` | QUERY | [10] Handling inlined equality of large integer |
| 37 | `Comparison1 - Equality-[11] Handling explicit equality of large integer` | QUERY | [11] Handling explicit equality of large integer |
| 38 | `Comparison1 - Equality-[12] Handling inlined equality of large integer, non-equal values` | QUERY | [12] Handling inlined equality of large integer, non-equal values |
| 39 | `Comparison1 - Equality-[13] Handling explicit equality of large integer, non-equal values` | QUERY | [13] Handling explicit equality of large integer, non-equal values |
| 40 | `Comparison1 - Equality-[14] Direction of traversed relationship is not significant for path equality, simple` | QUERY | [14] Direction of traversed relationship is not significant for path equality, s |
| 41 | `Comparison1 - Equality-[15] It is unknown - i.e. null - if a null is equal to a null` | QUERY | [15] It is unknown - i.e. null - if a null is equal to a null |
| 42 | `Comparison1 - Equality-[16] It is unknown - i.e. null - if a null is not equal to a null` | QUERY | [16] It is unknown - i.e. null - if a null is not equal to a null |
| 43 | `Comparison1 - Equality-[17] Failing when comparing to an undefined variable` | ERROR | [17] Failing when comparing to an undefined variable |

## Comparison2 (19/19 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Comparison2 - Half-bounded Range-[1] Comparing strings and integers using > in an AND'd predicate` | QUERY | [1] Comparing strings and integers using > in an AND'd predicate |
| 2 | `Comparison2 - Half-bounded Range-[2] Comparing strings and integers using > in a OR'd predicate` | QUERY | [2] Comparing strings and integers using > in a OR'd predicate |
| 3 | `Comparison2 - Half-bounded Range-[3] Comparing across types yields null, except numbers [operator=<, lhs=1, rhs=3.14]` | QUERY | [3] Comparing across types yields null, except numbers [operator=<, lhs=1, rhs=3 |
| 4 | `Comparison2 - Half-bounded Range-[3] Comparing across types yields null, except numbers [operator=<=, lhs=1, rhs=3.14]` | QUERY | [3] Comparing across types yields null, except numbers [operator=<=, lhs=1, rhs= |
| 5 | `Comparison2 - Half-bounded Range-[3] Comparing across types yields null, except numbers [operator=>=, lhs=3.14, rhs=1]` | QUERY | [3] Comparing across types yields null, except numbers [operator=>=, lhs=3.14, r |
| 6 | `Comparison2 - Half-bounded Range-[3] Comparing across types yields null, except numbers [operator=>, lhs=3.14, rhs=1]` | QUERY | [3] Comparing across types yields null, except numbers [operator=>, lhs=3.14, rh |
| 7 | `Comparison2 - Half-bounded Range-[4] Comparing lists [lhs=[1, 0], rhs=[1], result=true]` | QUERY | [4] Comparing lists [lhs=[1, 0], rhs=[1], result=true] |
| 8 | `Comparison2 - Half-bounded Range-[4] Comparing lists [lhs=[1, null], rhs=[1], result=true]` | QUERY | [4] Comparing lists [lhs=[1, null], rhs=[1], result=true] |
| 9 | `Comparison2 - Half-bounded Range-[4] Comparing lists [lhs=[1, 2], rhs=[1, null], result=null]` | QUERY | [4] Comparing lists [lhs=[1, 2], rhs=[1, null], result=null] |
| 10 | `Comparison2 - Half-bounded Range-[4] Comparing lists [lhs=[1, 'a'], rhs=[1, null], result=null]` | QUERY | [4] Comparing lists [lhs=[1, 'a'], rhs=[1, null], result=null] |
| 11 | `Comparison2 - Half-bounded Range-[4] Comparing lists [lhs=[1, 2], rhs=[3, null], result=false]` | QUERY | [4] Comparing lists [lhs=[1, 2], rhs=[3, null], result=false] |
| 12 | `Comparison2 - Half-bounded Range-[5] Comparing NaN [lhs=0.0 / 0.0, rhs=1, result=false]` | QUERY | [5] Comparing NaN [lhs=0.0 / 0.0, rhs=1, result=false] |
| 13 | `Comparison2 - Half-bounded Range-[5] Comparing NaN [lhs=0.0 / 0.0, rhs=1.0, result=false]` | QUERY | [5] Comparing NaN [lhs=0.0 / 0.0, rhs=1.0, result=false] |
| 14 | `Comparison2 - Half-bounded Range-[5] Comparing NaN [lhs=0.0 / 0.0, rhs=0.0 / 0.0, result=false]` | QUERY | [5] Comparing NaN [lhs=0.0 / 0.0, rhs=0.0 / 0.0, result=false] |
| 15 | `Comparison2 - Half-bounded Range-[5] Comparing NaN [lhs=0.0 / 0.0, rhs='a', result=null]` | QUERY | [5] Comparing NaN [lhs=0.0 / 0.0, rhs='a', result=null] |
| 16 | `Comparison2 - Half-bounded Range-[6] Comparability between numbers and strings [lhs=1.0, rhs=1.0, result=false]` | QUERY | [6] Comparability between numbers and strings [lhs=1.0, rhs=1.0, result=false] |
| 17 | `Comparison2 - Half-bounded Range-[6] Comparability between numbers and strings [lhs=1, rhs=1.0, result=false]` | QUERY | [6] Comparability between numbers and strings [lhs=1, rhs=1.0, result=false] |
| 18 | `Comparison2 - Half-bounded Range-[6] Comparability between numbers and strings [lhs='1.0', rhs=1.0, result=null]` | QUERY | [6] Comparability between numbers and strings [lhs='1.0', rhs=1.0, result=null] |
| 19 | `Comparison2 - Half-bounded Range-[6] Comparability between numbers and strings [lhs='1', rhs=1, result=null]` | QUERY | [6] Comparability between numbers and strings [lhs='1', rhs=1, result=null] |

## Comparison3 (9/9 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Comparison3 - Full-Bound Range-[1] Handling numerical ranges 1` | QUERY | [1] Handling numerical ranges 1 |
| 2 | `Comparison3 - Full-Bound Range-[2] Handling numerical ranges 2` | QUERY | [2] Handling numerical ranges 2 |
| 3 | `Comparison3 - Full-Bound Range-[3] Handling numerical ranges 3` | QUERY | [3] Handling numerical ranges 3 |
| 4 | `Comparison3 - Full-Bound Range-[4] Handling numerical ranges 4` | QUERY | [4] Handling numerical ranges 4 |
| 5 | `Comparison3 - Full-Bound Range-[5] Handling string ranges 1` | QUERY | [5] Handling string ranges 1 |
| 6 | `Comparison3 - Full-Bound Range-[6] Handling string ranges 2` | QUERY | [6] Handling string ranges 2 |
| 7 | `Comparison3 - Full-Bound Range-[7] Handling string ranges 3` | QUERY | [7] Handling string ranges 3 |
| 8 | `Comparison3 - Full-Bound Range-[8] Handling string ranges 4` | QUERY | [8] Handling string ranges 4 |
| 9 | `Comparison3 - Full-Bound Range-[9] Handling empty range` | QUERY | [9] Handling empty range |

## Comparison4 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Comparison4 - Combination of Comparisons-[1] Handling long chains of operators` | QUERY | [1] Handling long chains of operators |

## Literals1 (6/6 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals1 - Boolean and Null-[1] Return a boolean true lower case` | QUERY | [1] Return a boolean true lower case |
| 2 | `Literals1 - Boolean and Null-[2] Return a boolean true upper case` | QUERY | [2] Return a boolean true upper case |
| 3 | `Literals1 - Boolean and Null-[3] Return a boolean false lower case` | QUERY | [3] Return a boolean false lower case |
| 4 | `Literals1 - Boolean and Null-[4] Return a boolean false upper case` | QUERY | [4] Return a boolean false upper case |
| 5 | `Literals1 - Boolean and Null-[5] Return null lower case` | QUERY | [5] Return null lower case |
| 6 | `Literals1 - Boolean and Null-[6] Return null upper case` | QUERY | [6] Return null upper case |

## Literals2 (12/12 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals2 - Decimal integer-[1] Return a short positive integer` | QUERY | [1] Return a short positive integer |
| 2 | `Literals2 - Decimal integer-[2] Return a long positive integer` | QUERY | [2] Return a long positive integer |
| 3 | `Literals2 - Decimal integer-[3] Return the largest integer` | QUERY | [3] Return the largest integer |
| 4 | `Literals2 - Decimal integer-[4] Return a positive zero` | QUERY | [4] Return a positive zero |
| 5 | `Literals2 - Decimal integer-[5] Return a negative zero` | QUERY | [5] Return a negative zero |
| 6 | `Literals2 - Decimal integer-[6] Return a short negative integer` | QUERY | [6] Return a short negative integer |
| 7 | `Literals2 - Decimal integer-[7] Return a long negative integer` | QUERY | [7] Return a long negative integer |
| 8 | `Literals2 - Decimal integer-[8] Return the smallest integer` | QUERY | [8] Return the smallest integer |
| 9 | `Literals2 - Decimal integer-[9] Fail on a too large integer` | ERROR | [9] Fail on a too large integer |
| 10 | `Literals2 - Decimal integer-[10] Fail on a too small integer` | ERROR | [10] Fail on a too small integer |
| 11 | `Literals2 - Decimal integer-[11] Fail on an integer containing a alphabetic character` | ERROR | [11] Fail on an integer containing a alphabetic character |
| 12 | `Literals2 - Decimal integer-[12] Fail on an integer containing a invalid symbol character` | ERROR | [12] Fail on an integer containing a invalid symbol character |

## Literals3 (16/16 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals3 - Hexadecimal integer-[1] Return a short positive hexadecimal integer` | QUERY | [1] Return a short positive hexadecimal integer |
| 2 | `Literals3 - Hexadecimal integer-[2] Return a long positive hexadecimal integer` | QUERY | [2] Return a long positive hexadecimal integer |
| 3 | `Literals3 - Hexadecimal integer-[3] Return the largest hexadecimal integer` | QUERY | [3] Return the largest hexadecimal integer |
| 4 | `Literals3 - Hexadecimal integer-[4] Return a positive hexadecimal zero` | QUERY | [4] Return a positive hexadecimal zero |
| 5 | `Literals3 - Hexadecimal integer-[5] Return a negative hexadecimal zero` | QUERY | [5] Return a negative hexadecimal zero |
| 6 | `Literals3 - Hexadecimal integer-[6] Return a short negative hexadecimal integer` | QUERY | [6] Return a short negative hexadecimal integer |
| 7 | `Literals3 - Hexadecimal integer-[7] Return a long negative hexadecimal integer` | QUERY | [7] Return a long negative hexadecimal integer |
| 8 | `Literals3 - Hexadecimal integer-[8] Return the smallest hexadecimal integer` | QUERY | [8] Return the smallest hexadecimal integer |
| 9 | `Literals3 - Hexadecimal integer-[9] Return a lower case hexadecimal integer` | QUERY | [9] Return a lower case hexadecimal integer |
| 10 | `Literals3 - Hexadecimal integer-[10] Return a upper case hexadecimal integer` | QUERY | [10] Return a upper case hexadecimal integer |
| 11 | `Literals3 - Hexadecimal integer-[11] Return a mixed case hexadecimal integer` | QUERY | [11] Return a mixed case hexadecimal integer |
| 12 | `Literals3 - Hexadecimal integer-[12] Fail on an incomplete hexadecimal integer` | ERROR | [12] Fail on an incomplete hexadecimal integer |
| 13 | `Literals3 - Hexadecimal integer-[13] Fail on an hexadecimal literal containing a lower case invalid alphanumeric character` | ERROR | [13] Fail on an hexadecimal literal containing a lower case invalid alphanumeric |
| 14 | `Literals3 - Hexadecimal integer-[14] Fail on an hexadecimal literal containing a upper case invalid alphanumeric character` | ERROR | [14] Fail on an hexadecimal literal containing a upper case invalid alphanumeric |
| 15 | `Literals3 - Hexadecimal integer-[16] Fail on a too large hexadecimal integer` | ERROR | [16] Fail on a too large hexadecimal integer |
| 16 | `Literals3 - Hexadecimal integer-[17] Fail on a too small hexadecimal integer` | ERROR | [17] Fail on a too small hexadecimal integer |

## Literals4 (10/10 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals4 - Octal integer-[1] Return a short positive octal integer` | QUERY | [1] Return a short positive octal integer |
| 2 | `Literals4 - Octal integer-[2] Return a long positive octal integer` | QUERY | [2] Return a long positive octal integer |
| 3 | `Literals4 - Octal integer-[3] Return the largest octal integer` | QUERY | [3] Return the largest octal integer |
| 4 | `Literals4 - Octal integer-[4] Return a positive octal zero` | QUERY | [4] Return a positive octal zero |
| 5 | `Literals4 - Octal integer-[5] Return a negative octal zero` | QUERY | [5] Return a negative octal zero |
| 6 | `Literals4 - Octal integer-[6] Return a short negative octal integer` | QUERY | [6] Return a short negative octal integer |
| 7 | `Literals4 - Octal integer-[7] Return a long negative octal integer` | QUERY | [7] Return a long negative octal integer |
| 8 | `Literals4 - Octal integer-[8] Return the smallest octal integer` | QUERY | [8] Return the smallest octal integer |
| 9 | `Literals4 - Octal integer-[9] Fail on a too large octal integer` | ERROR | [9] Fail on a too large octal integer |
| 10 | `Literals4 - Octal integer-[10] Fail on a too small octal integer` | ERROR | [10] Fail on a too small octal integer |

## Literals5 (27/27 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals5 - Float-[1] Return a short positive float` | QUERY | [1] Return a short positive float |
| 2 | `Literals5 - Float-[2] Return a short positive float without integer digits` | QUERY | [2] Return a short positive float without integer digits |
| 3 | `Literals5 - Float-[3] Return a long positive float` | QUERY | [3] Return a long positive float |
| 4 | `Literals5 - Float-[4] Return a long positive float without integer digits` | QUERY | [4] Return a long positive float without integer digits |
| 5 | `Literals5 - Float-[5] Return a very long positive float` | QUERY | [5] Return a very long positive float |
| 6 | `Literals5 - Float-[6] Return a very long positive float without integer digits` | QUERY | [6] Return a very long positive float without integer digits |
| 7 | `Literals5 - Float-[7] Return a positive zero float` | QUERY | [7] Return a positive zero float |
| 8 | `Literals5 - Float-[8] Return a positive zero float without integer digits` | QUERY | [8] Return a positive zero float without integer digits |
| 9 | `Literals5 - Float-[9] Return a negative zero float` | QUERY | [9] Return a negative zero float |
| 10 | `Literals5 - Float-[10] Return a negative zero float without integer digits` | QUERY | [10] Return a negative zero float without integer digits |
| 11 | `Literals5 - Float-[11] Return a very long negative float` | QUERY | [11] Return a very long negative float |
| 12 | `Literals5 - Float-[12] Return a very long negative float without integer digits` | QUERY | [12] Return a very long negative float without integer digits |
| 13 | `Literals5 - Float-[13] Return a positive float with positive lower case exponent` | QUERY | [13] Return a positive float with positive lower case exponent |
| 14 | `Literals5 - Float-[14] Return a positive float with positive upper case exponent` | QUERY | [14] Return a positive float with positive upper case exponent |
| 15 | `Literals5 - Float-[15] Return a positive float with positive lower case exponent without integer digits` | QUERY | [15] Return a positive float with positive lower case exponent without integer d |
| 16 | `Literals5 - Float-[16] Return a positive float with negative lower case exponent` | QUERY | [16] Return a positive float with negative lower case exponent |
| 17 | `Literals5 - Float-[17] Return a positive float with negative lower case exponent without integer digits` | QUERY | [17] Return a positive float with negative lower case exponent without integer d |
| 18 | `Literals5 - Float-[18] Return a positive float with negative upper case exponent without integer digits` | QUERY | [18] Return a positive float with negative upper case exponent without integer d |
| 19 | `Literals5 - Float-[19] Return a negative float in with positive lower case exponent` | QUERY | [19] Return a negative float in with positive lower case exponent |
| 20 | `Literals5 - Float-[20] Return a negative float in with positive upper case exponent` | QUERY | [20] Return a negative float in with positive upper case exponent |
| 21 | `Literals5 - Float-[21] Return a negative float with positive lower case exponent without integer digits` | QUERY | [21] Return a negative float with positive lower case exponent without integer d |
| 22 | `Literals5 - Float-[22] Return a negative float with negative lower case exponent` | QUERY | [22] Return a negative float with negative lower case exponent |
| 23 | `Literals5 - Float-[23] Return a negative float with negative lower case exponent without integer digits` | QUERY | [23] Return a negative float with negative lower case exponent without integer d |
| 24 | `Literals5 - Float-[24] Return a negative float with negative upper case exponent without integer digits` | QUERY | [24] Return a negative float with negative upper case exponent without integer d |
| 25 | `Literals5 - Float-[25] Return a positive float with one integer digit and maximum positive exponent` | QUERY | [25] Return a positive float with one integer digit and maximum positive exponen |
| 26 | `Literals5 - Float-[26] Return a positive float with nine integer digit and maximum positive exponent` | QUERY | [26] Return a positive float with nine integer digit and maximum positive expone |
| 27 | `Literals5 - Float-[27] Fail when float value is too large` | ERROR | [27] Fail when float value is too large |

## Literals6 (13/13 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals6 - String-[1] Return a single-quoted empty string` | QUERY | [1] Return a single-quoted empty string |
| 2 | `Literals6 - String-[2] Return a single-quoted string with one character` | QUERY | [2] Return a single-quoted string with one character |
| 3 | `Literals6 - String-[3] Return a single-quoted string with uft-8 characters` | QUERY | [3] Return a single-quoted string with uft-8 characters |
| 4 | `Literals6 - String-[4] Return a single-quoted string with escaped single-quoted` | QUERY | [4] Return a single-quoted string with escaped single-quoted |
| 5 | `Literals6 - String-[5] Return a single-quoted string with escaped characters` | QUERY | [5] Return a single-quoted string with escaped characters |
| 6 | `Literals6 - String-[6] Return a single-quoted string with 100 characters` | QUERY | [6] Return a single-quoted string with 100 characters |
| 7 | `Literals6 - String-[7] Return a single-quoted string with 1000 characters` | QUERY | [7] Return a single-quoted string with 1000 characters |
| 8 | `Literals6 - String-[8] Return a single-quoted string with 10000 characters` | QUERY | [8] Return a single-quoted string with 10000 characters |
| 9 | `Literals6 - String-[9] Return a double-quoted empty string` | QUERY | [9] Return a double-quoted empty string |
| 10 | `Literals6 - String-[10] Accept valid Unicode literal` | QUERY | [10] Accept valid Unicode literal |
| 11 | `Literals6 - String-[11] Return a double-quoted string with one character` | QUERY | [11] Return a double-quoted string with one character |
| 12 | `Literals6 - String-[12] Return a double-quoted string with uft-8 characters` | QUERY | [12] Return a double-quoted string with uft-8 characters |
| 13 | `Literals6 - String-[13] Failing on incorrect unicode literal` | ERROR | [13] Failing on incorrect unicode literal |

## Literals7 (20/20 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals7 - List-[1] Return an empty list` | QUERY | [1] Return an empty list |
| 2 | `Literals7 - List-[2] Return a list containing a boolean` | QUERY | [2] Return a list containing a boolean |
| 3 | `Literals7 - List-[3] Return a list containing a null` | QUERY | [3] Return a list containing a null |
| 4 | `Literals7 - List-[4] Return a list containing a integer` | QUERY | [4] Return a list containing a integer |
| 5 | `Literals7 - List-[5] Return a list containing a hexadecimal integer` | QUERY | [5] Return a list containing a hexadecimal integer |
| 6 | `Literals7 - List-[6] Return a list containing a octal integer` | QUERY | [6] Return a list containing a octal integer |
| 7 | `Literals7 - List-[7] Return a list containing a float` | QUERY | [7] Return a list containing a float |
| 8 | `Literals7 - List-[8] Return a list containing a string` | QUERY | [8] Return a list containing a string |
| 9 | `Literals7 - List-[9] Return a list containing an empty lists` | QUERY | [9] Return a list containing an empty lists |
| 10 | `Literals7 - List-[10] Return seven-deep nested empty lists` | QUERY | [10] Return seven-deep nested empty lists |
| 11 | `Literals7 - List-[11] Return 20-deep nested empty lists` | QUERY | [11] Return 20-deep nested empty lists |
| 12 | `Literals7 - List-[12] Return 40-deep nested empty lists` | QUERY | [12] Return 40-deep nested empty lists |
| 13 | `Literals7 - List-[13] Return a list containing an empty map` | QUERY | [13] Return a list containing an empty map |
| 14 | `Literals7 - List-[14] Return a list containing multiple integer` | QUERY | [14] Return a list containing multiple integer |
| 15 | `Literals7 - List-[16] Return a list containing multiple mixed values` | QUERY | [16] Return a list containing multiple mixed values |
| 16 | `Literals7 - List-[17] Return a list containing real and fake nested lists` | QUERY | [17] Return a list containing real and fake nested lists |
| 17 | `Literals7 - List-[18] Return a complex list containing multiple mixed and nested values` | QUERY | [18] Return a complex list containing multiple mixed and nested values |
| 18 | `Literals7 - List-[19] Fail on a list containing only a comma` | ERROR | [19] Fail on a list containing only a comma |
| 19 | `Literals7 - List-[20] Fail on a nested list with non-matching brackets` | ERROR | [20] Fail on a nested list with non-matching brackets |
| 20 | `Literals7 - List-[21] Fail on a nested list with missing commas` | ERROR | [21] Fail on a nested list with missing commas |

## Literals8 (27/27 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Literals8 - Maps-[1] Return an empty map` | QUERY | [1] Return an empty map |
| 2 | `Literals8 - Maps-[2] Return a map containing one value with alphabetic lower case key` | QUERY | [2] Return a map containing one value with alphabetic lower case key |
| 3 | `Literals8 - Maps-[3] Return a map containing one value with alphabetic upper case key` | QUERY | [3] Return a map containing one value with alphabetic upper case key |
| 4 | `Literals8 - Maps-[4] Return a map containing one value with alphabetic mixed case key` | QUERY | [4] Return a map containing one value with alphabetic mixed case key |
| 5 | `Literals8 - Maps-[5] Return a map containing one value with alphanumeric mixed case key` | QUERY | [5] Return a map containing one value with alphanumeric mixed case key |
| 6 | `Literals8 - Maps-[6] Return a map containing a boolean` | QUERY | [6] Return a map containing a boolean |
| 7 | `Literals8 - Maps-[7] Return a map containing a null` | QUERY | [7] Return a map containing a null |
| 8 | `Literals8 - Maps-[8] Return a map containing a integer` | QUERY | [8] Return a map containing a integer |
| 9 | `Literals8 - Maps-[9] Return a map containing a hexadecimal integer` | QUERY | [9] Return a map containing a hexadecimal integer |
| 10 | `Literals8 - Maps-[10] Return a map containing a octal integer` | QUERY | [10] Return a map containing a octal integer |
| 11 | `Literals8 - Maps-[11] Return a map containing a float` | QUERY | [11] Return a map containing a float |
| 12 | `Literals8 - Maps-[12] Return a map containing a string` | QUERY | [12] Return a map containing a string |
| 13 | `Literals8 - Maps-[13] Return a map containing an empty map` | QUERY | [13] Return a map containing an empty map |
| 14 | `Literals8 - Maps-[14] Return seven-deep nested maps` | QUERY | [14] Return seven-deep nested maps |
| 15 | `Literals8 - Maps-[15] Return 20-deep nested maps` | QUERY | [15] Return 20-deep nested maps |
| 16 | `Literals8 - Maps-[16] Return 40-deep nested maps` | QUERY | [16] Return 40-deep nested maps |
| 17 | `Literals8 - Maps-[17] Return a map containing real and fake nested maps` | QUERY | [17] Return a map containing real and fake nested maps |
| 18 | `Literals8 - Maps-[18] Return a complex map containing multiple mixed and nested values` | QUERY | [18] Return a complex map containing multiple mixed and nested values |
| 19 | `Literals8 - Maps-[19] Fail on a map containing key starting with a number` | ERROR | [19] Fail on a map containing key starting with a number |
| 20 | `Literals8 - Maps-[20] Fail on a map containing key with symbol` | ERROR | [20] Fail on a map containing key with symbol |
| 21 | `Literals8 - Maps-[21] Fail on a map containing key with dot` | ERROR | [21] Fail on a map containing key with dot |
| 22 | `Literals8 - Maps-[22] Fail on a map containing unquoted string` | ERROR | [22] Fail on a map containing unquoted string |
| 23 | `Literals8 - Maps-[23] Fail on a map containing only a comma` | ERROR | [23] Fail on a map containing only a comma |
| 24 | `Literals8 - Maps-[24] Fail on a map containing a value without key` | ERROR | [24] Fail on a map containing a value without key |
| 25 | `Literals8 - Maps-[25] Fail on a map containing a list without key` | ERROR | [25] Fail on a map containing a list without key |
| 26 | `Literals8 - Maps-[26] Fail on a map containing a map without key` | ERROR | [26] Fail on a map containing a map without key |
| 27 | `Literals8 - Maps-[27] Fail on a nested map with non-matching braces` | ERROR | [27] Fail on a nested map with non-matching braces |

## Mathematical11 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Mathematical11 - Signed numbers functions-[1] Absolute function` | QUERY | [1] Absolute function |

## Mathematical13 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Mathematical13 - Square root-[1] `sqrt()` returning float values` | QUERY | [1] `sqrt()` returning float values |

## Mathematical2 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Mathematical2 - Addition-[1] Allow addition` | QUERY | [1] Allow addition |

## Mathematical3 (1/1 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Mathematical3 - Subtraction-[1] Fail for invalid Unicode hyphen in subtraction` | ERROR | [1] Fail for invalid Unicode hyphen in subtraction |

## Mathematical8 (2/2 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Mathematical8 - Arithmetic precedence-[1] Arithmetic precedence test` | QUERY | [1] Arithmetic precedence test |
| 2 | `Mathematical8 - Arithmetic precedence-[2] Arithmetic precedence with parenthesis test` | QUERY | [2] Arithmetic precedence with parenthesis test |

## Null1 (17/17 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Null1 - IS NULL validation-[1] Property null check on non-null node` | QUERY | [1] Property null check on non-null node |
| 2 | `Null1 - IS NULL validation-[2] Property null check on optional non-null node` | QUERY | [2] Property null check on optional non-null node |
| 3 | `Null1 - IS NULL validation-[3] Property null check on null node` | QUERY | [3] Property null check on null node |
| 4 | `Null1 - IS NULL validation-[4] A literal null IS null` | QUERY | [4] A literal null IS null |
| 5 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name, result=false]` | QUERY | [5] IS NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name, result=fals |
| 6 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name2, result=false]` | QUERY | [5] IS NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name2, result=fal |
| 7 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: 'Mats', name2: null}, key=name, result=false]` | QUERY | [5] IS NULL on a map [map={name: 'Mats', name2: null}, key=name, result=false] |
| 8 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: 'Mats', name2: null}, key=name2, result=true]` | QUERY | [5] IS NULL on a map [map={name: 'Mats', name2: null}, key=name2, result=true] |
| 9 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: null}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={name: null}, key=name, result=true] |
| 10 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: null, name2: null}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={name: null, name2: null}, key=name, result=true] |
| 11 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={name: null, name2: null}, key=name2, result=true]` | QUERY | [5] IS NULL on a map [map={name: null, name2: null}, key=name2, result=true] |
| 12 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={notName: null, notName2: null}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={notName: null, notName2: null}, key=name, result=true |
| 13 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={notName: 0, notName2: null}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={notName: 0, notName2: null}, key=name, result=true] |
| 14 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={notName: 0}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={notName: 0}, key=name, result=true] |
| 15 | `Null1 - IS NULL validation-[5] IS NULL on a map [map={}, key=name, result=true]` | QUERY | [5] IS NULL on a map [map={}, key=name, result=true] |
| 16 | `Null1 - IS NULL validation-[5] IS NULL on a map [map=null, key=name, result=true]` | QUERY | [5] IS NULL on a map [map=null, key=name, result=true] |
| 17 | `Null1 - IS NULL validation-[6] IS NULL is case insensitive` | QUERY | [6] IS NULL is case insensitive |

## Null2 (17/17 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Null2 - IS NOT NULL validation-[1] Property not null check on non-null node` | QUERY | [1] Property not null check on non-null node |
| 2 | `Null2 - IS NOT NULL validation-[2] Property not null check on optional non-null node` | QUERY | [2] Property not null check on optional non-null node |
| 3 | `Null2 - IS NOT NULL validation-[3] Property not null check on null node` | QUERY | [3] Property not null check on null node |
| 4 | `Null2 - IS NOT NULL validation-[4] A literal null is not IS NOT null` | QUERY | [4] A literal null is not IS NOT null |
| 5 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name, result=true]` | QUERY | [5] IS NOT NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name, result= |
| 6 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name2, result=true]` | QUERY | [5] IS NOT NULL on a map [map={name: 'Mats', name2: 'Pontus'}, key=name2, result |
| 7 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: 'Mats', name2: null}, key=name, result=true]` | QUERY | [5] IS NOT NULL on a map [map={name: 'Mats', name2: null}, key=name, result=true |
| 8 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: 'Mats', name2: null}, key=name2, result=false]` | QUERY | [5] IS NOT NULL on a map [map={name: 'Mats', name2: null}, key=name2, result=fal |
| 9 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: null}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={name: null}, key=name, result=false] |
| 10 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: null, name2: null}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={name: null, name2: null}, key=name, result=false] |
| 11 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={name: null, name2: null}, key=name2, result=false]` | QUERY | [5] IS NOT NULL on a map [map={name: null, name2: null}, key=name2, result=false |
| 12 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={notName: null, notName2: null}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={notName: null, notName2: null}, key=name, result= |
| 13 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={notName: 0, notName2: null}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={notName: 0, notName2: null}, key=name, result=fal |
| 14 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={notName: 0}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={notName: 0}, key=name, result=false] |
| 15 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map={}, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map={}, key=name, result=false] |
| 16 | `Null2 - IS NOT NULL validation-[5] IS NOT NULL on a map [map=null, key=name, result=false]` | QUERY | [5] IS NOT NULL on a map [map=null, key=name, result=false] |
| 17 | `Null2 - IS NOT NULL validation-[6] IS NOT NULL is case insensitive` | QUERY | [6] IS NOT NULL is case insensitive |

## Null3 (10/10 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Null3 - Null evaluation-[1] The inverse of a null is a null` | QUERY | [1] The inverse of a null is a null |
| 2 | `Null3 - Null evaluation-[2] It is unknown - i.e. null - if a null is equal to a null` | QUERY | [2] It is unknown - i.e. null - if a null is equal to a null |
| 3 | `Null3 - Null evaluation-[3] It is unknown - i.e. null - if a null is not equal to a null` | QUERY | [3] It is unknown - i.e. null - if a null is not equal to a null |
| 4 | `Null3 - Null evaluation-[4] Using null in IN [elt=null, coll=null, result=null]` | QUERY | [4] Using null in IN [elt=null, coll=null, result=null] |
| 5 | `Null3 - Null evaluation-[4] Using null in IN [elt=null, coll=[1, 2, 3], result=null]` | QUERY | [4] Using null in IN [elt=null, coll=[1, 2, 3], result=null] |
| 6 | `Null3 - Null evaluation-[4] Using null in IN [elt=null, coll=[1, 2, 3, null], result=null]` | QUERY | [4] Using null in IN [elt=null, coll=[1, 2, 3, null], result=null] |
| 7 | `Null3 - Null evaluation-[4] Using null in IN [elt=null, coll=[], result=false]` | QUERY | [4] Using null in IN [elt=null, coll=[], result=false] |
| 8 | `Null3 - Null evaluation-[4] Using null in IN [elt=1, coll=[1, 2, 3, null], result=true]` | QUERY | [4] Using null in IN [elt=1, coll=[1, 2, 3, null], result=true] |
| 9 | `Null3 - Null evaluation-[4] Using null in IN [elt=1, coll=[null, 1], result=true]` | QUERY | [4] Using null in IN [elt=1, coll=[null, 1], result=true] |
| 10 | `Null3 - Null evaluation-[4] Using null in IN [elt=5, coll=[1, 2, 3, null], result=null]` | QUERY | [4] Using null in IN [elt=5, coll=[1, 2, 3, null], result=null] |

## Pattern1 (39/39 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Pattern1 - Pattern predicate-[1] Matching on any single outgoing directed connection` | QUERY | [1] Matching on any single outgoing directed connection |
| 2 | `Pattern1 - Pattern predicate-[2] Matching on a single undirected connection` | QUERY | [2] Matching on a single undirected connection |
| 3 | `Pattern1 - Pattern predicate-[3] Matching on any single incoming directed connection` | QUERY | [3] Matching on any single incoming directed connection |
| 4 | `Pattern1 - Pattern predicate-[4] Matching on a specific type of single outgoing directed connection` | QUERY | [4] Matching on a specific type of single outgoing directed connection |
| 5 | `Pattern1 - Pattern predicate-[5] Matching on a specific type of single undirected connection` | QUERY | [5] Matching on a specific type of single undirected connection |
| 6 | `Pattern1 - Pattern predicate-[6] Matching on a specific type of single incoming directed connection` | QUERY | [6] Matching on a specific type of single incoming directed connection |
| 7 | `Pattern1 - Pattern predicate-[7] Matching on a specific type of a variable length outgoing directed connection` | QUERY | [7] Matching on a specific type of a variable length outgoing directed connectio |
| 8 | `Pattern1 - Pattern predicate-[8] Matching on a specific type of variable length undirected connection` | QUERY | [8] Matching on a specific type of variable length undirected connection |
| 9 | `Pattern1 - Pattern predicate-[9] Matching on a specific type of variable length incoming directed connection` | QUERY | [9] Matching on a specific type of variable length incoming directed connection |
| 10 | `Pattern1 - Pattern predicate-[10] Matching on a specific type of undirected connection with length 2` | QUERY | [10] Matching on a specific type of undirected connection with length 2 |
| 11 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(a)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(a)] |
| 12 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]->(a)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]->(a)] |
| 13 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(a)-[r]->(n)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(a)-[r]->(n)] |
| 14 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)<-[r {}]-(a)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)<-[r {}]-(a) |
| 15 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r {}]-(a)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r {}]-(a)] |
| 16 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]->()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]->()] |
| 17 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=()-[r]->(n)]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=()-[r]->(n)] |
| 18 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)<-[r]-()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)<-[r]-()] |
| 19 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]-()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r]-()] |
| 20 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=()-[r]->()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=()-[r]->()] |
| 21 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=()<-[r]-()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=()<-[r]-()] |
| 22 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=()-[r]-()]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=()-[r]-()] |
| 23 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL]->(a {num: 5})]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL]->(a |
| 24 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL*0..2]->(a {num: 5})]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL*0..2 |
| 25 | `Pattern1 - Pattern predicate-[10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL]->(:C)<-[s:REL]-(a {num: 5})]` | ERROR | [10] Fail on introducing unbounded variables in pattern [pattern=(n)-[r:REL]->(: |
| 26 | `Pattern1 - Pattern predicate-[11] Fail on checking self pattern` | ERROR | [11] Fail on checking self pattern |
| 27 | `Pattern1 - Pattern predicate-[12] Matching two nodes on a single directed connection between them` | QUERY | [12] Matching two nodes on a single directed connection between them |
| 28 | `Pattern1 - Pattern predicate-[13] Fail on matching two nodes on a single undirected connection between them` | QUERY | [13] Fail on matching two nodes on a single undirected connection between them |
| 29 | `Pattern1 - Pattern predicate-[14] Matching two nodes on a specific type of single outgoing directed connection` | QUERY | [14] Matching two nodes on a specific type of single outgoing directed connectio |
| 30 | `Pattern1 - Pattern predicate-[15] Matching two nodes on a specific type of single undirected connection` | QUERY | [15] Matching two nodes on a specific type of single undirected connection |
| 31 | `Pattern1 - Pattern predicate-[16] Matching two nodes on a specific type of a variable length outgoing directed connection` | QUERY | [16] Matching two nodes on a specific type of a variable length outgoing directe |
| 32 | `Pattern1 - Pattern predicate-[17] Matching two nodes on a specific type of variable length undirected connection` | QUERY | [17] Matching two nodes on a specific type of variable length undirected connect |
| 33 | `Pattern1 - Pattern predicate-[18] Matching two nodes on a specific type of undirected connection with length 2` | QUERY | [18] Matching two nodes on a specific type of undirected connection with length  |
| 34 | `Pattern1 - Pattern predicate-[19] Using a negated existential pattern predicate` | QUERY | [19] Using a negated existential pattern predicate |
| 35 | `Pattern1 - Pattern predicate-[20] Using two existential pattern predicates in a conjunction` | QUERY | [20] Using two existential pattern predicates in a conjunction |
| 36 | `Pattern1 - Pattern predicate-[21] Using two existential pattern predicates in a disjunction` | QUERY | [21] Using two existential pattern predicates in a disjunction |
| 37 | `Pattern1 - Pattern predicate-[22] Fail on using pattern in RETURN projection` | ERROR | [22] Fail on using pattern in RETURN projection |
| 38 | `Pattern1 - Pattern predicate-[23] Fail on using pattern in WITH projection` | ERROR | [23] Fail on using pattern in WITH projection |
| 39 | `Pattern1 - Pattern predicate-[24] Fail on using pattern in right-hand side of SET` | ERROR | [24] Fail on using pattern in right-hand side of SET |

## Pattern2 (11/11 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Pattern2 - Pattern Comprehension-[1] Return a pattern comprehension` | QUERY | [1] Return a pattern comprehension |
| 2 | `Pattern2 - Pattern Comprehension-[2] Return a pattern comprehension with label predicate` | QUERY | [2] Return a pattern comprehension with label predicate |
| 3 | `Pattern2 - Pattern Comprehension-[3] Return a pattern comprehension with bound nodes` | QUERY | [3] Return a pattern comprehension with bound nodes |
| 4 | `Pattern2 - Pattern Comprehension-[4] Introduce a new node variable in pattern comprehension` | QUERY | [4] Introduce a new node variable in pattern comprehension |
| 5 | `Pattern2 - Pattern Comprehension-[5] Introduce a new relationship variable in pattern comprehension` | QUERY | [5] Introduce a new relationship variable in pattern comprehension |
| 6 | `Pattern2 - Pattern Comprehension-[6] Aggregate on a pattern comprehension` | QUERY | [6] Aggregate on a pattern comprehension |
| 7 | `Pattern2 - Pattern Comprehension-[7] Use a pattern comprehension inside a list comprehension` | QUERY | [7] Use a pattern comprehension inside a list comprehension |
| 8 | `Pattern2 - Pattern Comprehension-[8] Use a pattern comprehension in WITH` | QUERY | [8] Use a pattern comprehension in WITH |
| 9 | `Pattern2 - Pattern Comprehension-[9] Use a variable-length pattern comprehension in WITH` | QUERY | [9] Use a variable-length pattern comprehension in WITH |
| 10 | `Pattern2 - Pattern Comprehension-[10] Use a pattern comprehension in RETURN` | QUERY | [10] Use a pattern comprehension in RETURN |
| 11 | `Pattern2 - Pattern Comprehension-[11] Use a pattern comprehension and ORDER BY` | QUERY | [11] Use a pattern comprehension and ORDER BY |

## Precedence1 (72/72 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Precedence1 - On boolean values-[1] Exclusive disjunction takes precedence over inclusive disjunction` | QUERY | [1] Exclusive disjunction takes precedence over inclusive disjunction |
| 2 | `Precedence1 - On boolean values-[2] Conjunction disjunction takes precedence over exclusive disjunction` | QUERY | [2] Conjunction disjunction takes precedence over exclusive disjunction |
| 3 | `Precedence1 - On boolean values-[3] Conjunction disjunction takes precedence over inclusive disjunction` | QUERY | [3] Conjunction disjunction takes precedence over inclusive disjunction |
| 4 | `Precedence1 - On boolean values-[4] Negation takes precedence over conjunction` | QUERY | [4] Negation takes precedence over conjunction |
| 5 | `Precedence1 - On boolean values-[5] Negation takes precedence over inclusive disjunction` | QUERY | [5] Negation takes precedence over inclusive disjunction |
| 6 | `Precedence1 - On boolean values-[6] Comparison operator takes precedence over boolean negation` | QUERY | [6] Comparison operator takes precedence over boolean negation |
| 7 | `Precedence1 - On boolean values-[7] Comparison operator takes precedence over binary boolean operator` | QUERY | [7] Comparison operator takes precedence over binary boolean operator |
| 8 | `Precedence1 - On boolean values-[8] Null predicate takes precedence over comparison operator` | QUERY | [8] Null predicate takes precedence over comparison operator |
| 9 | `Precedence1 - On boolean values-[9] Null predicate takes precedence over negation` | QUERY | [9] Null predicate takes precedence over negation |
| 10 | `Precedence1 - On boolean values-[10] Null predicate takes precedence over boolean operator` | QUERY | [10] Null predicate takes precedence over boolean operator |
| 11 | `Precedence1 - On boolean values-[11] List predicate takes precedence over comparison operator` | QUERY | [11] List predicate takes precedence over comparison operator |
| 12 | `Precedence1 - On boolean values-[12] List predicate takes precedence over negation` | QUERY | [12] List predicate takes precedence over negation |
| 13 | `Precedence1 - On boolean values-[13] List predicate takes precedence over boolean operator` | QUERY | [13] List predicate takes precedence over boolean operator |
| 14 | `Precedence1 - On boolean values-[14] Exclusive disjunction takes precedence over inclusive disjunction in every combination of truth values` | QUERY | [14] Exclusive disjunction takes precedence over inclusive disjunction in every  |
| 15 | `Precedence1 - On boolean values-[15] Conjunction takes precedence over exclusive disjunction in every combination of truth values` | QUERY | [15] Conjunction takes precedence over exclusive disjunction in every combinatio |
| 16 | `Precedence1 - On boolean values-[16] Conjunction takes precedence over inclusive disjunction in every combination of truth values` | QUERY | [16] Conjunction takes precedence over inclusive disjunction in every combinatio |
| 17 | `Precedence1 - On boolean values-[17] Negation takes precedence over conjunction in every combination of truth values` | QUERY | [17] Negation takes precedence over conjunction in every combination of truth va |
| 18 | `Precedence1 - On boolean values-[18] Negation takes precedence over inclusive disjunction in every combination of truth values` | QUERY | [18] Negation takes precedence over inclusive disjunction in every combination o |
| 19 | `Precedence1 - On boolean values-[19] Comparison operators takes precedence over boolean negation in every combination of truth values [comp=<=]` | QUERY | [19] Comparison operators takes precedence over boolean negation in every combin |
| 20 | `Precedence1 - On boolean values-[19] Comparison operators takes precedence over boolean negation in every combination of truth values [comp=>=]` | QUERY | [19] Comparison operators takes precedence over boolean negation in every combin |
| 21 | `Precedence1 - On boolean values-[19] Comparison operators takes precedence over boolean negation in every combination of truth values [comp=<]` | QUERY | [19] Comparison operators takes precedence over boolean negation in every combin |
| 22 | `Precedence1 - On boolean values-[19] Comparison operators takes precedence over boolean negation in every combination of truth values [comp=>]` | QUERY | [19] Comparison operators takes precedence over boolean negation in every combin |
| 23 | `Precedence1 - On boolean values-[20] Pairs of comparison operators and boolean negation that are associative in every combination of truth values [comp==]` | QUERY | [20] Pairs of comparison operators and boolean negation that are associative in  |
| 24 | `Precedence1 - On boolean values-[20] Pairs of comparison operators and boolean negation that are associative in every combination of truth values [comp=<>]` | QUERY | [20] Pairs of comparison operators and boolean negation that are associative in  |
| 25 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred==, boolop=OR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 26 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred==, boolop=AND]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 27 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<=, boolop=OR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 28 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<=, boolop=XOR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 29 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<=, boolop=AND]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 30 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=>=, boolop=XOR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 31 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=>=, boolop=AND]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 32 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<, boolop=OR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 33 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<, boolop=XOR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 34 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<, boolop=AND]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 35 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=>, boolop=OR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 36 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=>, boolop=XOR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 37 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<>, boolop=OR]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 38 | `Precedence1 - On boolean values-[21] Comparison operators take precedence over binary boolean operators in every combination of truth values [pred=<>, boolop=AND]` | QUERY | [21] Comparison operators take precedence over binary boolean operators in every |
| 39 | `Precedence1 - On boolean values-[22] Pairs of comparison operators and binary boolean operators that are associative in every combination of truth values [pred==, boolop=XOR]` | QUERY | [22] Pairs of comparison operators and binary boolean operators that are associa |
| 40 | `Precedence1 - On boolean values-[22] Pairs of comparison operators and binary boolean operators that are associative in every combination of truth values [pred=>=, boolop=OR]` | QUERY | [22] Pairs of comparison operators and binary boolean operators that are associa |
| 41 | `Precedence1 - On boolean values-[22] Pairs of comparison operators and binary boolean operators that are associative in every combination of truth values [pred=>, boolop=AND]` | QUERY | [22] Pairs of comparison operators and binary boolean operators that are associa |
| 42 | `Precedence1 - On boolean values-[22] Pairs of comparison operators and binary boolean operators that are associative in every combination of truth values [pred=<>, boolop=XOR]` | QUERY | [22] Pairs of comparison operators and binary boolean operators that are associa |
| 43 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp==, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 44 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp==, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 45 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<=, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 46 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<=, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 47 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=>=, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 48 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=>=, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 49 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 50 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 51 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=>, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 52 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=>, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 53 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<>, nullpred=IS NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 54 | `Precedence1 - On boolean values-[23] Null predicates take precedence over comparison operators in every combination of truth values [comp=<>, nullpred=IS NOT NULL]` | QUERY | [23] Null predicates take precedence over comparison operators in every combinat |
| 55 | `Precedence1 - On boolean values-[24] Null predicates take precedence over boolean negation on every truth values [nullpred=IS NULL]` | QUERY | [24] Null predicates take precedence over boolean negation on every truth values |
| 56 | `Precedence1 - On boolean values-[24] Null predicates take precedence over boolean negation on every truth values [nullpred=IS NOT NULL]` | QUERY | [24] Null predicates take precedence over boolean negation on every truth values |
| 57 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=OR, nullpred=IS NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 58 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=OR, nullpred=IS NOT NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 59 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=XOR, nullpred=IS NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 60 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=XOR, nullpred=IS NOT NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 61 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=AND, nullpred=IS NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 62 | `Precedence1 - On boolean values-[25] Null predicates take precedence over binary boolean operators in every combination of truth values [boolop=AND, nullpred=IS NOT NULL]` | QUERY | [25] Null predicates take precedence over binary boolean operators in every comb |
| 63 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp==]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 64 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp=<=]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 65 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp=>=]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 66 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp=<]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 67 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp=>]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 68 | `Precedence1 - On boolean values-[26] List predicate takes precedence over comparison operators in every combination of truth values [comp=<>]` | QUERY | [26] List predicate takes precedence over comparison operators in every combinat |
| 69 | `Precedence1 - On boolean values-[27] List predicate takes precedence over negation in every combination of truth values` | QUERY | [27] List predicate takes precedence over negation in every combination of truth |
| 70 | `Precedence1 - On boolean values-[28] List predicate takes precedence over binary boolean operators in every combination of truth values [boolop=OR]` | QUERY | [28] List predicate takes precedence over binary boolean operators in every comb |
| 71 | `Precedence1 - On boolean values-[28] List predicate takes precedence over binary boolean operators in every combination of truth values [boolop=XOR]` | QUERY | [28] List predicate takes precedence over binary boolean operators in every comb |
| 72 | `Precedence1 - On boolean values-[28] List predicate takes precedence over binary boolean operators in every combination of truth values [boolop=AND]` | QUERY | [28] List predicate takes precedence over binary boolean operators in every comb |

## Precedence2 (26/26 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=+, mult2=*, right=14, wrong=40]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 2 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=+, mult2=/, right=9, wrong=10]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 3 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=+, mult2=%, right=9, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 4 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=-, mult2=*, right=2, wrong=-8]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 5 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=-, mult2=/, right=7, wrong=-2]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 6 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=*, add=-, mult2=%, right=7, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 7 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=+, mult2=*, right=8, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 8 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=+, mult2=/, right=3, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 9 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=+, mult2=%, right=3, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 10 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=-, mult2=*, right=-4, wrong=-8]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 11 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=-, mult2=/, right=1, wrong=-2]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 12 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=/, add=-, mult2=%, right=1, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 13 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=+, mult2=*, right=6, wrong=8]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 14 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=+, mult2=/, right=1, wrong=2]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 15 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=+, mult2=%, right=1, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 16 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=-, mult2=*, right=-6, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 17 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=-, mult2=/, right=-1, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 18 | `Precedence2 - On numeric values-[1] Numeric multiplicative operations takes precedence over numeric additive operations [mult1=%, add=-, mult2=%, right=-1, wrong=0]` | QUERY | [1] Numeric multiplicative operations takes precedence over numeric additive ope |
| 19 | `Precedence2 - On numeric values-[2] Exponentiation takes precedence over numeric multiplicative operations [mult=*, right=512.0, wrong=68719476736.0]` | QUERY | [2] Exponentiation takes precedence over numeric multiplicative operations [mult |
| 20 | `Precedence2 - On numeric values-[2] Exponentiation takes precedence over numeric multiplicative operations [mult=/, right=8.0, wrong=64.0]` | QUERY | [2] Exponentiation takes precedence over numeric multiplicative operations [mult |
| 21 | `Precedence2 - On numeric values-[2] Exponentiation takes precedence over numeric multiplicative operations [mult=%, right=0.0, wrong=64.0]` | QUERY | [2] Exponentiation takes precedence over numeric multiplicative operations [mult |
| 22 | `Precedence2 - On numeric values-[3] Exponentiation takes precedence over numeric additive operations [add=+, right=72.0, wrong=1073741824.0]` | QUERY | [3] Exponentiation takes precedence over numeric additive operations [add=+, rig |
| 23 | `Precedence2 - On numeric values-[3] Exponentiation takes precedence over numeric additive operations [add=-, right=56.0, wrong=64.0]` | QUERY | [3] Exponentiation takes precedence over numeric additive operations [add=-, rig |
| 24 | `Precedence2 - On numeric values-[4] Numeric unary negative takes precedence over exponentiation` | QUERY | [4] Numeric unary negative takes precedence over exponentiation |
| 25 | `Precedence2 - On numeric values-[5] Numeric unary negative takes precedence over numeric additive operations [add=+, right=-1, wrong=-5]` | QUERY | [5] Numeric unary negative takes precedence over numeric additive operations [ad |
| 26 | `Precedence2 - On numeric values-[5] Numeric unary negative takes precedence over numeric additive operations [add=-, right=-5, wrong=-1]` | QUERY | [5] Numeric unary negative takes precedence over numeric additive operations [ad |

## Precedence3 (11/11 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Precedence3 - On list values-[1] List element access takes precedence over list appending` | QUERY | [1] List element access takes precedence over list appending |
| 2 | `Precedence3 - On list values-[2] List element access takes precedence over list concatenation` | QUERY | [2] List element access takes precedence over list concatenation |
| 3 | `Precedence3 - On list values-[3] List slicing takes precedence over list concatenation` | QUERY | [3] List slicing takes precedence over list concatenation |
| 4 | `Precedence3 - On list values-[4] List appending takes precedence over list element containment` | QUERY | [4] List appending takes precedence over list element containment |
| 5 | `Precedence3 - On list values-[5] List concatenation takes precedence over list element containment` | QUERY | [5] List concatenation takes precedence over list element containment |
| 6 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp==, right=false, wrong=true]` | QUERY | [6] List element containment takes precedence over comparison operator [comp==,  |
| 7 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp=<>, right=true, wrong=false]` | QUERY | [6] List element containment takes precedence over comparison operator [comp=<>, |
| 8 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp=<, right=null, wrong=false]` | QUERY | [6] List element containment takes precedence over comparison operator [comp=<,  |
| 9 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp=>, right=null, wrong=true]` | QUERY | [6] List element containment takes precedence over comparison operator [comp=>,  |
| 10 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp=<=, right=null, wrong=false]` | QUERY | [6] List element containment takes precedence over comparison operator [comp=<=, |
| 11 | `Precedence3 - On list values-[6] List element containment takes precedence over comparison operator [comp=>=, right=null, wrong=true]` | QUERY | [6] List element containment takes precedence over comparison operator [comp=>=, |

## Precedence4 (12/12 extracted)

| # | ID | Type | Description |
|---|-----|------|-------------|
| 1 | `Precedence4 - On null value-[1] Null predicate takes precedence over comparison operator [null1=NOT NULL, comp==, null2=NULL, right=false, wrong=true]` | QUERY | [1] Null predicate takes precedence over comparison operator [null1=NOT NULL, co |
| 2 | `Precedence4 - On null value-[1] Null predicate takes precedence over comparison operator [null1=NULL, comp=<>, null2=NULL, right=false, wrong=true]` | QUERY | [1] Null predicate takes precedence over comparison operator [null1=NULL, comp=< |
| 3 | `Precedence4 - On null value-[1] Null predicate takes precedence over comparison operator [null1=NULL, comp=<>, null2=NOT NULL, right=true, wrong=false]` | QUERY | [1] Null predicate takes precedence over comparison operator [null1=NULL, comp=< |
| 4 | `Precedence4 - On null value-[2] Null predicate takes precedence over boolean negation` | QUERY | [2] Null predicate takes precedence over boolean negation |
| 5 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=null, op=AND, truth2=null, null=NULL, right=null, wrong=true]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=null, o |
| 6 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=null, op=AND, truth2=true, null=NULL, right=false, wrong=true]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=null, o |
| 7 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=false, op=AND, truth2=false, null=NOT NULL, right=false, wrong=true]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=false,  |
| 8 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=null, op=OR, truth2=false, null=NULL, right=null, wrong=true]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=null, o |
| 9 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=true, op=OR, truth2=null, null=NULL, right=true, wrong=false]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=true, o |
| 10 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=true, op=XOR, truth2=null, null=NOT NULL, right=true, wrong=false]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=true, o |
| 11 | `Precedence4 - On null value-[3] Null predicate takes precedence over binary boolean operator [truth1=true, op=XOR, truth2=false, null=NULL, right=true, wrong=false]` | QUERY | [3] Null predicate takes precedence over binary boolean operator [truth1=true, o |
| 12 | `Precedence4 - On null value-[4] String predicate takes precedence over binary boolean operator` | QUERY | [4] String predicate takes precedence over binary boolean operator |
