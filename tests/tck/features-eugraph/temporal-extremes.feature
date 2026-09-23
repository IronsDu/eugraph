Feature: Temporal extremes stay correct

  Scenario: extreme year span in duration.between
    Given an empty graph
    When executing query:
      """
      RETURN toString(duration.between(date('-999999999-01-01'), date('+999999999-12-31'))) AS d
      """
    Then the result should be, in any order:
      | d                    |
      | 'P1999999998Y11M30D' |
    And no side effects

  Scenario: extreme year span in duration.inSeconds
    Given an empty graph
    When executing query:
      """
      RETURN toString(duration.inSeconds(localdatetime('-999999999-01-01'), localdatetime('+999999999-12-31T23:59:59'))) AS d
      """
    Then the result should be, in any order:
      | d                         |
      | 'PT17531639991215H59M59S' |
    And no side effects

  Scenario: expanded year rendering matches neo4j
    Given an empty graph
    When executing query:
      """
      RETURN toString(date({year: 11476, month: 8, day: 15})) AS a, toString(datetime.fromepoch(100000000000, 0)) AS b
      """
    Then the result should be, in any order:
      | a             | b                      |
      | '+11476-08-15' | '5138-11-16T09:46:40Z' |
    And no side effects
