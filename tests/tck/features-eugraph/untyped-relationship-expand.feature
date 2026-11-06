Feature: Untyped relationship expansion

  # `-[]->` means "any relationship type", not "an explicit type set that
  # happens to contain every catalog label". These scenarios pin both the
  # result semantics and the same-endpoint self-loop corner case that depends
  # on the exact label seek.

  Background:
    Given an empty graph

  Scenario: Untyped fixed-length expand visits every relationship type
    And having executed:
      """
      CREATE (a:A)-[:R]->(:B), (a)-[:S]->(:C)
      """
    When executing query:
      """
      MATCH (a:A)-[r]->(m)
      RETURN count(r) AS c
      """
    Then the result should be, in any order:
      | c |
      | 2 |
    And no side effects

  Scenario: Explicitly listing every relationship type is still a typed set
    And having executed:
      """
      CREATE (a:A)-[:R]->(:B), (a)-[:S]->(:C)
      """
    When executing query:
      """
      MATCH (a:A)-[:R|S]->(m)
      RETURN count(m) AS c
      """
    Then the result should be, in any order:
      | c |
      | 2 |
    And no side effects

  Scenario: Untyped undirected same-endpoint expand keeps only self-loops
    And having executed:
      """
      CREATE (a:A {id: 1})-[:LOOP]->(a), (b:A {id: 2})-[:LOOP]->(a)
      """
    When executing query:
      """
      MATCH (a:A {id: 1})-[r]-(a)
      RETURN count(r) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |
    And no side effects

  Scenario: Untyped variable-length expand visits every relationship type
    And having executed:
      """
      CREATE (a:A)-[:R]->(:B)-[:S]->(:C)
      """
    When executing query:
      """
      MATCH (a:A)-[*1..2]->(c:C)
      RETURN count(c) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |
    And no side effects
