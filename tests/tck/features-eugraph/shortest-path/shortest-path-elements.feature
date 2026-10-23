Feature: shortestPath path elements and direction

  Scenario: allShortestPaths distinguishes parallel edges
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (a)-[:K]->(b), (a)-[:K]->(b)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (b:Person {name: 'b'})
      MATCH p = allShortestPaths((a)-[:K*1..2]->(b))
      RETURN count(*) AS paths
      """
    Then the result should be, in any order:
      | paths |
      | 2     |
    And no side effects

  Scenario: relationships(p) preserves physical edge direction
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})-[:K]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:K*1..3]->(c))
      RETURN [r IN relationships(p) | [startNode(r).name, endNode(r).name]] AS edges
      """
    Then the result should be, in any order:
      | edges                     |
      | [['a', 'b'], ['b', 'c']] |
    And no side effects

  Scenario: a reverse pattern keeps physical edge direction
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})-[:K]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((c)<-[:K*1..3]-(a))
      RETURN [r IN relationships(p) | [startNode(r).name, endNode(r).name]] AS edges
      """
    Then the result should be, in any order:
      | edges                     |
      | [['b', 'c'], ['a', 'b']] |
    And no side effects

  Scenario: a wrong directed pattern returns no path
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (b:Person {name: 'b'})
      MATCH p = shortestPath((a)<-[:K*1..3]-(b))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
    And no side effects

  Scenario: an edge type filter constrains the shortest path
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (c:Person {name: 'c'}),
             (a)-[:K]->(b), (b)-[:K]->(c), (a)-[:L]->(c)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:K*1..3]->(c))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 2 |
    And no side effects

  Scenario: multiple endpoint rows are searched independently
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'})-[:K]->(b:Person {name: 'b'})
             -[:K]->(c:Person {name: 'c'}),
             (d:Person {name: 'd'})-[:K]->(e:Person {name: 'e'})
      """
    When executing query:
      """
      UNWIND [['a', 'c'], ['d', 'e']] AS pair
      MATCH (s:Person {name: pair[0]}), (t:Person {name: pair[1]})
      MATCH p = shortestPath((s)-[:K*1..3]->(t))
      RETURN s.name AS s, t.name AS t, length(p) AS n
      """
    Then the result should be, in any order:
      | s | t | n |
      | 'a' | 'c' | 2 |
      | 'd' | 'e' | 1 |
    And no side effects

  Scenario: shortestPath can bind fresh endpoints from the pattern itself
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})-[:K]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH p = shortestPath((a:Person {name: 'a'})-[:K*1..3]->(c:Person {name: 'c'}))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 2 |
    And no side effects

  Scenario: allShortestPaths returns no rows when no path exists
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'}), (:Person {name: 'z'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (z:Person {name: 'z'})
      MATCH p = allShortestPaths((a)-[:K*1..3]->(z))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
    And no side effects

  Scenario: a cycle does not make shortestPath loop forever
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (c:Person {name: 'c'}),
             (a)-[:K]->(b), (b)-[:K]->(a), (a)-[:K]->(c)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:K*1..8]->(c))
      RETURN length(p) AS n, [x IN nodes(p) | x.name] AS names
      """
    Then the result should be, in any order:
      | n | names         |
      | 1 | ['a', 'c']   |
    And no side effects

  Scenario: allShortestPaths supports the zero-length path
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'})
      MATCH p = allShortestPaths((a)-[:K*0..0]->(a))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 0 |
    And no side effects
