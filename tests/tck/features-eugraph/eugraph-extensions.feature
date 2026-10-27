Feature: Eugraph extensions

  Scenario: date - date keeps duration.between semantics
    Given an empty graph
    When executing query:
      """
      RETURN date('2024-03-01') - date('2024-01-31') AS d
      """
    Then the result should be, in any order:
      | d      |
      | 'P1M1D' |
    And no side effects

  Scenario: duration ordering comparisons are null like neo4j
    Given an empty graph
    When executing query:
      """
      RETURN duration({seconds: 1}) < duration({seconds: 2}) AS lt, duration({seconds: 1}) <= duration({seconds: 1}) AS le
      """
    Then the result should be, in any order:
      | lt   | le   |
      | null | true |
    And no side effects

  Scenario: group by a list containing null
    Given an empty graph
    When executing query:
      """
      UNWIND [1, 1] AS x
      WITH [null] AS l, count(*) AS c
      RETURN l, c
      """
    Then the result should be, in any order:
      | l      | c |
      | [null] | 2 |
    And no side effects
