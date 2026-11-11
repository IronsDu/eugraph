Feature: Correlated endpoint and size-only list optimizations

  # Correctness regressions for the four correlated-endpoint optimizations:
  #   A1 batch exact seek, A2 endpoint filter folding, B destination-set probe,
  #   C destination-label filter cost model, D topology-only list elements.
  # Every expected result below was produced by the local neo4j SF1 instance
  # (a temporary Tck* subgraph) and matches eugraph row for row.

  Background:
    Given an empty graph

  Scenario: Correlated EXISTS with a bound destination keeps the endpoint constraint
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}), (c:TckB {id: 3}), (d:TckB {id: 4}),
             (a)-[:TCK_R]->(b), (a)-[:TCK_R]->(b), (a)-[:TCK_S]->(c), (a)-[:TCK_R]->(a)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (b:TckB)
      WHERE EXISTS { (a)-[:TCK_R]->(b) }
      RETURN b.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 2  |
    And no side effects

  Scenario: Parallel edges still count once per edge for a bound endpoint
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}),
             (a)-[:TCK_R]->(b), (a)-[:TCK_R]->(b)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (b:TckB)
      RETURN b.id AS id, size([(a)-[:TCK_R]->(b) | 1]) AS n
      ORDER BY id
      """
    Then the result should be, in order:
      | id | n |
      | 2  | 2 |
    And no side effects

  Scenario: Directed self-loop with a bound destination matches itself
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}),
             (a)-[:TCK_R]->(a), (a)-[:TCK_S]->(b)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (x:TckA)
      WHERE EXISTS { (a)-[:TCK_R]->(x) }
      RETURN x.id AS id
      """
    Then the result should be, in order:
      | id |
      | 1  |
    And no side effects

  Scenario: Undirected self-loop is counted once, not once per adjacency list
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (a)-[:TCK_R]->(a)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (x:TckA)
      RETURN x.id AS id, size([(a)-[:TCK_R]-(x) | 1]) AS n
      """
    Then the result should be, in order:
      | id | n |
      | 1  | 1 |
    And no side effects

  Scenario: Undirected edge with a bound destination matches once
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}), (a)-[:TCK_R]->(b)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (b:TckB {id: 2})
      RETURN size([(a)-[:TCK_R]-(b) | 1]) AS n
      """
    Then the result should be, in order:
      | n |
      | 1 |
    And no side effects

  Scenario: Multiple relationship types with a bound destination
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}), (c:TckB {id: 3}),
             (a)-[:TCK_R]->(b), (a)-[:TCK_S]->(c)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (b:TckB)
      WHERE EXISTS { (a)-[:TCK_R|TCK_S]->(b) }
      RETURN b.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 2  |
      | 3  |
    And no side effects

  Scenario: NOT EXISTS with a bound destination keeps the anti-join semantics
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}), (c:TckB {id: 3}),
             (a)-[:TCK_R]->(b)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1}), (b:TckB)
      WHERE NOT EXISTS { (a)-[:TCK_R]-(b) }
      RETURN b.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 3  |
    And no side effects

  Scenario: User-written id() equality is not folded into the Expand
    Given having executed:
      """
      CREATE (a:TckA {id: 1}), (b:TckB {id: 2}),
             (a)-[:TCK_R]->(a), (a)-[:TCK_R]->(b)
      """
    When executing query:
      """
      MATCH (a:TckA {id: 1})-[r:TCK_R]->(b)
      WHERE id(a) = id(b)
      RETURN b.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 1  |
    And no side effects

  Scenario: Destination label filter keeps only the matching label (small neighbour set)
    Given having executed:
      """
      CREATE (f:TckF {id: 1})
      """
    And having executed:
      """
      UNWIND range(1, 30) AS i CREATE (:TckX {id: i})
      """
    And having executed:
      """
      MATCH (f:TckF {id: 1}), (x:TckX {id: 1}) CREATE (x)-[:TCK_C]->(f)
      """
    When executing query:
      """
      MATCH (f:TckF {id: 1})<-[:TCK_C]-(x:TckX)
      RETURN x.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 1  |
    And no side effects

  Scenario: Destination label filter keeps only the matching label (large neighbour set)
    Given having executed:
      """
      CREATE (f:TckF {id: 1})
      """
    And having executed:
      """
      UNWIND range(1, 30) AS i CREATE (:TckX {id: i})
      """
    And having executed:
      """
      MATCH (f:TckF {id: 1}), (x:TckX) WHERE x.id <= 10 CREATE (x)-[:TCK_C]->(f)
      """
    When executing query:
      """
      MATCH (f:TckF {id: 1})<-[:TCK_C]-(x:TckX)
      RETURN x.id AS id
      ORDER BY id
      """
    Then the result should be, in order:
      | id |
      | 1  |
      | 2  |
      | 3  |
      | 4  |
      | 5  |
      | 6  |
      | 7  |
      | 8  |
      | 9  |
      | 10 |
    And no side effects

  Scenario: size-only list elements stay topological and count correctly
    Given having executed:
      """
      CREATE (p:TckP {id: 1})-[:TCK_INTEREST]->(t10:TckT {id: 10})
      """
    And having executed:
      """
      CREATE (f:TckF {id: 2}),
             (post:TckPost {id: 20})-[:TCK_CREATED]->(f),
             (other:TckPost {id: 21})-[:TCK_CREATED]->(f)
      """
    And having executed:
      """
      MATCH (post:TckPost {id: 20}), (t10:TckT {id: 10})
      CREATE (post)-[:TCK_HAS_TAG]->(t10)
      """
    And having executed:
      """
      MATCH (other:TckPost {id: 21})
      CREATE (other)-[:TCK_HAS_TAG]->(:TckT {id: 99})
      """
    When executing query:
      """
      MATCH (p:TckP {id: 1}), (f:TckF {id: 2})
      OPTIONAL MATCH (f)<-[:TCK_CREATED]-(post:TckPost)
      WITH p, f, collect(post) AS posts
      RETURN f.id AS fid, size(posts) AS n,
             size([x IN posts WHERE (x)-[:TCK_HAS_TAG]->()<-[:TCK_INTEREST]-(p)]) AS matched
      """
    Then the result should be, in order:
      | fid | n | matched |
      | 2   | 2 | 1       |
    And no side effects

  Scenario: Two matching tags on one element still count that element once
    Given having executed:
      """
      CREATE (p:TckP {id: 1})-[:TCK_INTEREST]->(t10:TckT {id: 10})
      """
    And having executed:
      """
      MATCH (p:TckP {id: 1}) CREATE (p)-[:TCK_INTEREST]->(t11:TckT {id: 11})
      """
    And having executed:
      """
      CREATE (f:TckF {id: 2}), (post:TckPost {id: 20})-[:TCK_CREATED]->(f)
      """
    And having executed:
      """
      MATCH (post:TckPost {id: 20}), (t10:TckT {id: 10}), (t11:TckT {id: 11})
      CREATE (post)-[:TCK_HAS_TAG]->(t10), (post)-[:TCK_HAS_TAG]->(t11)
      """
    When executing query:
      """
      MATCH (p:TckP {id: 1}), (f:TckF {id: 2})
      OPTIONAL MATCH (f)<-[:TCK_CREATED]-(post:TckPost)
      WITH p, f, collect(post) AS posts
      RETURN f.id AS fid, size(posts) AS n,
             size([x IN posts WHERE (x)-[:TCK_HAS_TAG]->()<-[:TCK_INTEREST]-(p)]) AS matched
      """
    Then the result should be, in order:
      | fid | n | matched |
      | 2   | 1 | 1       |
    And no side effects

  Scenario: Observing the collected list through head keeps full vertices
    Given having executed:
      """
      CREATE (p:TckP {id: 1})-[:TCK_INTEREST]->(t10:TckT {id: 10})
      """
    And having executed:
      """
      CREATE (f:TckF {id: 2}), (post:TckPost {id: 20})-[:TCK_CREATED]->(f)
      """
    And having executed:
      """
      MATCH (post:TckPost {id: 20}), (t10:TckT {id: 10})
      CREATE (post)-[:TCK_HAS_TAG]->(t10)
      """
    When executing query:
      """
      MATCH (p:TckP {id: 1}), (f:TckF {id: 2})
      OPTIONAL MATCH (f)<-[:TCK_CREATED]-(post:TckPost)
      WITH p, f, collect(post) AS posts
      RETURN f.id AS fid,
             size([x IN posts WHERE (x)-[:TCK_HAS_TAG]->()<-[:TCK_INTEREST]-(p)]) AS matched,
             head(posts).id AS first
      """
    Then the result should be, in order:
      | fid | matched | first |
      | 2   | 1       | 20    |
    And no side effects

  Scenario: A second UNWIND over the collected list disables the topology-only path
    Given having executed:
      """
      CREATE (p:TckP {id: 1})-[:TCK_INTEREST]->(t10:TckT {id: 10})
      """
    And having executed:
      """
      CREATE (f:TckF {id: 2}),
             (post:TckPost {id: 20})-[:TCK_CREATED]->(f),
             (other:TckPost {id: 21})-[:TCK_CREATED]->(f)
      """
    And having executed:
      """
      MATCH (post:TckPost {id: 20}), (t10:TckT {id: 10})
      CREATE (post)-[:TCK_HAS_TAG]->(t10)
      """
    When executing query:
      """
      MATCH (p:TckP {id: 1}), (f:TckF {id: 2})
      OPTIONAL MATCH (f)<-[:TCK_CREATED]-(post:TckPost)
      WITH p, f, collect(post) AS posts
      UNWIND posts AS q
      RETURN f.id AS fid,
             size([x IN posts WHERE (x)-[:TCK_HAS_TAG]->()<-[:TCK_INTEREST]-(p)]) AS matched,
             q.id AS qid
      ORDER BY qid
      """
    Then the result should be, in order:
      | fid | matched | qid |
      | 2   | 1       | 20  |
      | 2   | 1       | 21  |
    And no side effects
