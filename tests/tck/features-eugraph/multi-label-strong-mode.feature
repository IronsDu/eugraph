Feature: Multi-label nodes and strong label-scoped access

  Background:
    Given an empty graph

  # Strong access is label-scoped: the cast selects the property namespace.
  Scenario: Strong access reads a property from the cast label
    When executing query:
      """
      CREATE (n:`Person` {name: 'Alice'})
      """
    Then the result should be empty

    When executing query:
      """
      MATCH (n:`Person` {name: 'Alice'})
      RETURN n::`Person`.name AS person_name
      """
    Then the result should be, in any order:
      | person_name |
      | 'Alice'     |

  # A label introduced only as a tag has no property schema. This catches the
  # whole-vertex leak in RETURN n::Label: before the fix keys(n::Employee) was 1.
  Scenario: A tag-only label exposes no properties through a strong cast
    When executing query:
      """
      CREATE (n:`Person`:`Employee` {name: 'Alice'})
      """
    Then the result should be empty

    When executing query:
      """
      MATCH (n:`Person`:`Employee`)
      RETURN size(keys(n::`Person`)) AS person_fields,
             size(keys(n::`Employee`)) AS employee_fields
      """
    Then the result should be, in any order:
      | person_fields | employee_fields |
      | 1             | 0               |
