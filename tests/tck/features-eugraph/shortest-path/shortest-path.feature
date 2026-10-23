Feature: shortestPath and allShortestPaths

  Scenario: shortestPath returns one shortest chain
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:KNOWS]->(:Person {name: 'b'})-[:KNOWS]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:KNOWS*1..3]-(c))
      RETURN length(p) AS len, [n IN nodes(p) | n.name] AS names
      """
    Then the result should be, in any order:
      | len | names               |
      | 2   | ['a', 'b', 'c']     |
    And no side effects

  Scenario: allShortestPaths returns every equal-length chain
    Given an empty graph
    And having executed:
      """
      CREATE (a:Person {name: 'a'}),
             (b:Person {name: 'b'}),
             (c:Person {name: 'c'}),
             (d:Person {name: 'd'}),
             (a)-[:KNOWS]->(b), (b)-[:KNOWS]->(d),
             (a)-[:KNOWS]->(c), (c)-[:KNOWS]->(d)
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (d:Person {name: 'd'})
      MATCH p = allShortestPaths((a)-[:KNOWS*1..3]-(d))
      RETURN count(*) AS paths
      """
    Then the result should be, in any order:
      | paths |
      | 2     |
    And no side effects

  Scenario: shortestPath returns no rows when no path exists
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'}), (:Person {name: 'z'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (z:Person {name: 'z'})
      MATCH p = shortestPath((a)-[:KNOWS*1..3]-(z))
      RETURN length(p) AS len
      """
    Then the result should be, in any order:
      | len |
    And no side effects

  Scenario: shortestPath respects the direction of a directed chain
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:KNOWS]->(:Person {name: 'b'})-[:KNOWS]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((a)-[:KNOWS*1..3]->(c))
      RETURN [n IN nodes(p) | n.name] AS names
      """
    Then the result should be, in any order:
      | names           |
      | ['a', 'b', 'c'] |
    And no side effects

  Scenario: shortestPath follows a reverse pattern from the other endpoint
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {name: 'a'})-[:KNOWS]->(:Person {name: 'b'})-[:KNOWS]->(:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (a:Person {name: 'a'}), (c:Person {name: 'c'})
      MATCH p = shortestPath((c)<-[:KNOWS*1..3]-(a))
      RETURN [n IN nodes(p) | n.name] AS names
      """
    Then the result should be, in any order:
      | names           |
      | ['c', 'b', 'a'] |
    And no side effects
