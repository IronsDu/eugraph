# Property ownership - DDL/DML boundary
#
# DML never defines fields: a label created by CREATE has no columns, and an
# existing label never grows a column because a property was written. Schema
# changes only come from DDL. Contract: docs/features/strong-weak-hybrid.md §1.

Feature: Property ownership - schema boundary

  Background:
    Given an empty graph

  Scenario: DML on an unknown label leaves the label empty
    When executing query:
      """
      CREATE (n:AutoNew {p: 'x'})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABEL AutoNew
      """
    Then the result should be empty
    When executing query:
      """
      MATCH (n:AutoNew) RETURN n.p AS p
      """
    Then the result should be, in any order:
      | p   |
      | 'x' |
    When executing query:
      """
      DESCRIBE LABEL __anon__
      """
    Then the result should be, in any order:
      | label      | propertyName | propertyType |
      | '__anon__' | 'p'          | 'ANY'        |

  Scenario: DML does not extend an existing label
    Given the schema:
      """
      CREATE LABEL KnownSchema (id:INT64)
      """
    When executing query:
      """
      CREATE (n:KnownSchema {id: 1, extra: 'x'})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABEL KnownSchema
      """
    Then the result should be, in any order:
      | label         | propertyName | propertyType |
      | 'KnownSchema' | 'id'         | 'INTEGER'    |
    When executing query:
      """
      MATCH (n:KnownSchema) RETURN n.extra AS e
      """
    Then the result should be, in any order:
      | e   |
      | 'x' |
