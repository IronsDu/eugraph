Feature: Variable-length relationship property filters from expressions

  # Inline relationship properties in a variable-length pattern may reference
  # outer variables; each input row is evaluated independently before DFS.

  Background:
    Given an empty graph

  Scenario: Filter value references the start node property
    Given having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS {score: 'a'}]->(b:Person {name: 'b'})
             -[:KNOWS {score: 'b'}]->(c:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH p = (a:Person {name: 'a'})-[:KNOWS*1..2 {score: a.name}]->(b)
      RETURN length(p) AS n
      """
    Then the result should be, in any order:
      | n |
      | 1 |
    And no side effects

  Scenario: Each source row evaluates its own expression value
    Given having executed:
      """
      CREATE (a1:Person {age: 1})-[:RATED {score: 1}]->(b1:Person),
             (a2:Person {age: 2})-[:RATED {score: 2}]->(b2:Person)
      """
    When executing query:
      """
      MATCH (a:Person)-[:RATED*1 {score: a.age}]->(b)
      RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 2 |
    And no side effects

  Scenario: Non-matching expression excludes the edge
    Given having executed:
      """
      CREATE (a:Person {age: 1})-[:RATED {score: 9}]->(b:Person)
      """
    When executing query:
      """
      MATCH (a:Person)-[:RATED*1 {score: a.age}]->(b)
      RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 0 |
    And no side effects
