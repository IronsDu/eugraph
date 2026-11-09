Feature: Nested EXISTS predicates

  # Regression coverage for nested pattern EXISTS correlation: the inner
  # subquery starts from the outer EXISTS' chain variable (m), not from the
  # statement's original MATCH variable (n).

  Background:
    Given an empty graph

  Scenario: Nested pattern EXISTS correlates the outer chain node
    Given having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})-[:KNOWS]->(c:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (n:Person)
      WHERE EXISTS {
        (n)-[:KNOWS]->(m)
        WHERE EXISTS { (m)-[:KNOWS]->() }
      }
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name |
      | 'a'  |
    And no side effects

  Scenario: Nested pattern EXISTS can be negated
    Given having executed:
      """
      CREATE (a:Person {name: 'a'})-[:KNOWS]->(b:Person {name: 'b'})-[:KNOWS]->(c:Person {name: 'c'})
      """
    When executing query:
      """
      MATCH (n:Person)
      WHERE NOT EXISTS {
        (n)-[:KNOWS]->(m)
        WHERE EXISTS { (m)-[:KNOWS]->() }
      }
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name |
      | 'b'  |
      | 'c'  |
    And no side effects
