Feature: shortestPath hop bounds

  # Neo4j 5 rejects shortestPath/allShortestPaths with min hops > 1
  # ("does not support a minimal length different from 0 or 1"), and rejects
  # start == end by default. Those eugraph extensions are therefore not part
  # of this cross-engine TCK feature; only the max-bound behavior is.

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
