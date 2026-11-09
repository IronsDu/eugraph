Feature: EXISTS in projection and CASE

  Background:
    Given an empty graph

  Scenario: Full EXISTS subquery in RETURN is correlated per row
    Given having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})
      """
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name,
             EXISTS { MATCH (n)-[:KNOWS]->(:Person) RETURN 1 } AS has_out
      ORDER BY name
      """
    Then the result should be, in any order:
      | name | has_out |
      | 'a'  | true    |
      | 'b'  | false   |
    And no side effects

  Scenario: Full EXISTS subquery in CASE
    Given having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})
      """
    When executing query:
      """
      MATCH (n:Person)
      RETURN n.name AS name,
             CASE WHEN EXISTS { MATCH (n)-[:KNOWS]->() RETURN 1 } THEN 'yes' ELSE 'no' END AS result
      ORDER BY name
      """
    Then the result should be, in any order:
      | name | result |
      | 'a'  | 'yes'  |
      | 'b'  | 'no'   |
    And no side effects

