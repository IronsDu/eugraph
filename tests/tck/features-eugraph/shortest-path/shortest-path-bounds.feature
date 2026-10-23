Feature: shortestPath hop bounds

  Scenario: the lower bound selects a longer path when a shorter path exists
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (c:Person {name: 'c'}), (d:Person {name: 'd'}),
             (a)-[:K]->(b), (b)-[:K]->(c), (c)-[:K]->(d), (a)-[:K]->(d)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (d:Person {name: 'd'})
      MATCH p = shortestPath((a)-[:K*2..3]->(d))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 3 |
    And no side effects

  Scenario: allShortestPaths uses the same lower bound
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (c:Person {name: 'c'}), (d:Person {name: 'd'}),
             (a)-[:K]->(b), (b)-[:K]->(c), (c)-[:K]->(d), (a)-[:K]->(d)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (d:Person {name: 'd'})
      MATCH p = allShortestPaths((a)-[:K*2..3]->(d))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 3 |
    And no side effects

  Scenario: an exact lower bound with no path at that length is empty
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}), (b:Person {name: 'b'}),
             (c:Person {name: 'c'}), (d:Person {name: 'd'}),
             (a)-[:K]->(b), (b)-[:K]->(c), (c)-[:K]->(d), (a)-[:K]->(d)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (d:Person {name: 'd'})
      MATCH p = shortestPath((a)-[:K*2..2]->(d))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
    And no side effects

  Scenario: no path when the shortest path exceeds max hops
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})-[:K]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:K*1..1]->(c))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
    And no side effects

  Scenario: zero-length shortestPath between a node and itself
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'})
      MATCH p = shortestPath((a)-[:K*0..0]->(a))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 0 |
    And no side effects

  Scenario: allShortestPaths is empty when the shortest path exceeds max hops
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:K]->(:Person {name: 'b'})-[:K]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = allShortestPaths((a)-[:K*1..1]->(c))
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
    And no side effects
