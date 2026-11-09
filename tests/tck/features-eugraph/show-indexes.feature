Feature: SHOW INDEXES

  Background:
    Given an empty graph

  Scenario: SHOW INDEXES reports a created label index
    Given having executed:
      """
      CREATE INDEX idx_person_name FOR (n:Person) ON (n.name)
      """
    When executing query:
      """
      SHOW INDEXES
      """
    Then the result should be, in any order:
      | name            | label    | property | unique | state    |
      | 'idx_person_name' | 'Person' | 'name'   | 'false' | 'PUBLIC' |
    And no side effects
