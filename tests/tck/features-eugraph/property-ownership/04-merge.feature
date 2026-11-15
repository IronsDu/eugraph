# Property ownership - MERGE
#
# MERGE writes undeclared keys into the weak space and matches them there.
# Contract: docs/features/strong-weak-hybrid.md §5.

Feature: Property ownership - MERGE

  Background:
    Given an empty graph

  Scenario: MERGE writes an undeclared key to the weak space and matches it
    Given the schema:
      """
      CREATE LABEL MergeId (id INT64)
      """
    When executing query:
      """
      MERGE (n:MergeId {id: 1, tag: 'x'})
      """
    Then the result should be empty
    When executing query:
      """
      MERGE (n:MergeId {id: 1, tag: 'x'}) RETURN n.tag AS t
      """
    Then the result should be, in any order:
      | t   |
      | 'x' |
    When executing query:
      """
      MATCH (n:MergeId) RETURN count(*) AS c
      """
    Then the result should be, in any order:
      | c |
      | 1 |
    When executing query:
      """
      DESCRIBE LABEL MergeId
      """
    Then the result should be, in any order:
      | label     | propertyName | propertyType |
      | 'MergeId' | 'id'         | 'INTEGER'    |

  Scenario: ON CREATE SET writes an undeclared key to the weak space
    Given the schema:
      """
      CREATE LABEL MergeOnCreate (id INT64)
      """
    When executing query:
      """
      MERGE (n:MergeOnCreate {id: 2}) ON CREATE SET n.extra = 42 RETURN n.extra AS e
      """
    Then the result should be, in any order:
      | e  |
      | 42 |
    When executing query:
      """
      MATCH (n:MergeOnCreate) RETURN n.extra AS e
      """
    Then the result should be, in any order:
      | e  |
      | 42 |
