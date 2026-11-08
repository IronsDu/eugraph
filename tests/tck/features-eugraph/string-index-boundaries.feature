Feature: String index exactness, prefixes and composite indexed columns

  Scenario: Exact string index lookup does not match longer strings
    Given an empty graph
    And having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.name)
      """
    And having executed:
      """
      CREATE (:Person {name: 'Jose'}),
             (:Person {name: 'Joseph'}),
             (:Person {name: 'Josef'}),
             (:Person {name: 'Jose Trinidad'})
      """
    When executing query:
      """
      MATCH (p:Person) WHERE p.name = 'Jose' RETURN p.name
      """
    Then the result should be, in any order:
      | p.name |
      | 'Jose' |
    And no side effects

  Scenario: String index prefix lookup includes exact and longer values
    Given an empty graph
    And having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.name)
      """
    And having executed:
      """
      CREATE (:Person {name: 'Jose'}),
             (:Person {name: 'Joseph'}),
             (:Person {name: 'Josef'}),
             (:Person {name: 'Jose Trinidad'}),
             (:Person {name: 'Alice'})
      """
    When executing query:
      """
      MATCH (p:Person) WHERE p.name STARTS WITH 'Jose' RETURN p.name
      """
    Then the result should be, in any order:
      | p.name |
      | 'Jose' |
      | 'Joseph' |
      | 'Josef' |
      | 'Jose Trinidad' |
    And no side effects

  Scenario: Composite index returns both indexed string columns
    Given an empty graph
    And having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.firstName, n.lastName)
      """
    And having executed:
      """
      CREATE (:Person {firstName: 'Jose', lastName: 'A'}),
             (:Person {firstName: 'Joseph', lastName: 'A'}),
             (:Person {firstName: 'Jose', lastName: 'B'}),
             (:Person {firstName: 'Alice', lastName: 'A'})
      """
    When executing query:
      """
      MATCH (p:Person)
      WHERE p.firstName = 'Jose' AND p.lastName = 'A'
      RETURN p.firstName, p.lastName
      """
    Then the result should be, in any order:
      | p.firstName | p.lastName |
      | 'Jose'      | 'A'        |
    And no side effects

  Scenario: Composite index prefix on the last column returns both indexed strings
    Given an empty graph
    And having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.firstName, n.lastName)
      """
    And having executed:
      """
      CREATE (:Person {firstName: 'Jose', lastName: 'A'}),
             (:Person {firstName: 'Joseph', lastName: 'A'}),
             (:Person {firstName: 'Jose', lastName: 'B'}),
             (:Person {firstName: 'Alice', lastName: 'A'})
      """
    When executing query:
      """
      MATCH (p:Person)
      WHERE p.firstName = 'Jose' AND p.lastName STARTS WITH ''
      RETURN p.firstName, p.lastName
      """
    Then the result should be, in any order:
      | p.firstName | p.lastName |
      | 'Jose'      | 'A'        |
      | 'Jose'      | 'B'        |
    And no side effects

  Scenario: Composite index leading-column prefix returns both indexed strings
    Given an empty graph
    And having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.firstName, n.lastName)
      """
    And having executed:
      """
      CREATE (:Person {firstName: 'Jose', lastName: 'A'}),
             (:Person {firstName: 'Joseph', lastName: 'A'}),
             (:Person {firstName: 'Jose', lastName: 'B'}),
             (:Person {firstName: 'Alice', lastName: 'A'})
      """
    When executing query:
      """
      MATCH (p:Person)
      WHERE p.firstName STARTS WITH 'Jose'
      RETURN p.firstName, p.lastName
      """
    Then the result should be, in any order:
      | p.firstName | p.lastName |
      | 'Jose'      | 'A'        |
      | 'Jose'      | 'B'        |
      | 'Joseph'    | 'A'        |
    And no side effects

  Scenario: String index prefix comparison against a full scan
    Given an empty graph
    And having executed:
      """
      CREATE (:Person {firstName: 'Jose', lastName: 'A'}),
             (:Person {firstName: 'Joseph', lastName: 'A'}),
             (:Person {firstName: 'Jose', lastName: 'B'}),
             (:Person {firstName: 'Alice', lastName: 'A'})
      """
    When executing query:
      """
      MATCH (p:Person)
      WHERE p.firstName STARTS WITH 'Jose'
      RETURN p.firstName, p.lastName
      """
    Then the result should be, in any order:
      | p.firstName | p.lastName |
      | 'Jose'      | 'A'        |
      | 'Jose'      | 'B'        |
      | 'Joseph'    | 'A'        |
    And no side effects
