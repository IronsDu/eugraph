Feature: Vertex summary invalidation is transparent to Cypher

  Scenario: Same statement insert and property update sees the new value
    Given an empty graph
    When executing query:
      """
      CREATE (n:SummaryPerson {name: 'Alice'})
      SET n.name = 'Alicia'
      RETURN n.name AS name
      """
    Then the result should be, in any order:
      | name     |
      | 'Alicia' |

  Scenario: Same statement insert and second property update keeps both values
    Given an empty graph
    When executing query:
      """
      CREATE (n:SummaryPerson {name: 'Alice'})
      SET n.age = 31
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age |
      | 'Alice' | 31  |

  Scenario: Property update after insert is visible
    Given an empty graph
    And having executed:
      """
      CREATE (:SummaryPerson {name: 'Alice', age: 30})
      """
    And having executed:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      SET n.age = 31
      """
    When executing query:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age |
      | 'Alice' | 31  |
    And no side effects

  Scenario: Removed property is null after invalidating the summary
    Given an empty graph
    And having executed:
      """
      CREATE (:SummaryPerson {name: 'Alice', age: 30})
      """
    And having executed:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      SET n.age = null
      """
    When executing query:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      RETURN n.name AS name, n.age AS age
      """
    Then the result should be, in any order:
      | name    | age  |
      | 'Alice' | null |
    And no side effects

  Scenario: Added label is visible after summary invalidation
    Given an empty graph
    And having executed:
      """
      CREATE (:SummaryPerson {name: 'Alice'})
      """
    And having executed:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      SET n:SummaryEmployee
      """
    When executing query:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      RETURN n.name AS name, n:SummaryEmployee AS is_employee
      """
    Then the result should be, in any order:
      | name    | is_employee |
      | 'Alice' | true        |
    And no side effects

  Scenario: Removed label is not resurrected from the summary
    Given an empty graph
    And having executed:
      """
      CREATE (:SummaryPerson:SummaryEmployee {name: 'Alice'})
      """
    And having executed:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      REMOVE n:SummaryEmployee
      """
    When executing query:
      """
      MATCH (n:SummaryPerson {name: 'Alice'})
      RETURN n.name AS name, n:SummaryEmployee AS is_employee
      """
    Then the result should be, in any order:
      | name    | is_employee |
      | 'Alice' | false       |
    And no side effects
