Feature: Large durations do not overflow

  Scenario: adding a large duration keeps sub-second precision
    Given an empty graph
    When executing query:
      """
      RETURN toString(localtime({hour: 12, minute: 31, second: 14, nanosecond: 1}) - duration({years: 12, months: 5, days: 14, hours: 16, minutes: 12, seconds: 70, nanoseconds: 2})) AS t
      """
    Then the result should be, in any order:
      | t                    |
      | '20:18:03.999999999' |
    And no side effects

  Scenario: adding a large duration to a date
    Given an empty graph
    When executing query:
      """
      RETURN toString(date('2024-01-01') + duration({seconds: 10000000000})) AS d
      """
    Then the result should be, in any order:
      | d            |
      | '2340-11-20' |
    And no side effects

  Scenario: multiplying a duration by a double keeps seconds and nanos separate
    Given an empty graph
    When executing query:
      """
      RETURN toString(duration({seconds: 1000000000000, nanoseconds: 500000000}) * 1.5) AS d
      """
    Then the result should be, in any order:
      | d                       |
      | 'PT416666666H40M0.75S'  |
    And no side effects

  Scenario: a list shared by UNWIND is not emptied element by element
    Given an empty graph
    When executing query:
      """
      WITH [[1], [1, 2]] AS vs
      UNWIND vs AS v
      RETURN size([x IN vs WHERE x < v]) AS n
        ORDER BY v
      """
    Then the result should be, in order:
      | n |
      | 0 |
      | 1 |
    And no side effects
