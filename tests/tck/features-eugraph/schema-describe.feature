Feature: Schema introspection

  Background:
    Given an empty graph

  # Fresh graphs always carry the anonymous label (GraphManager installs it so
  # unlabeled nodes have somewhere to put their properties), so DESCRIBE LABELS
  # reports exactly one row here. That also pins the record shape.
  Scenario: A fresh graph reports the anonymous label and nothing else
    When executing query:
      """
      DESCRIBE LABELS
      """
    Then the result should be, in any order:
      | name       | anonymous |
      | '__anon__' | true      |

  Scenario: The anonymous label is not reported twice and stays flagged
    When executing query:
      """
      CREATE (:Person {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABELS
      """
    Then the result should be, in any order:
      | name       | anonymous |
      | '__anon__' | true      |
      | 'Person'   | false     |

  # Field types come from the label's declared schema. A bare CREATE registers
  # newly-seen properties as ANY (see tck-guide.md), so this case asserts the
  # field is reported and its shape, not a specific scalar type.
  Scenario: DESCRIBE LABEL reports the fields of one label
    When executing query:
      """
      CREATE (:Person {name: 'Alice', age: 30})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABEL Person
      """
    Then the result should be, in any order:
      | label    | propertyName | propertyType |
      | 'Person' | 'age'        | 'ANY'        |
      | 'Person' | 'name'        | 'ANY'        |

  Scenario: DESCRIBE LABEL is case-sensitive on the label name
    When executing query:
      """
      CREATE (:Person {id: 1})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABEL person
      """
    Then the result should be empty

  Scenario: DESCRIBE RELATIONSHIP reports the fields of one relationship type
    When executing query:
      """
      CREATE (:Person {id: 1})-[:KNOWS {since: 2020}]->(:Person {id: 2})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE RELATIONSHIP KNOWS
      """
    Then the result should be, in any order:
      | relType | propertyName | propertyType |
      | 'KNOWS' | 'since'        | 'INTEGER'    |

  Scenario: DESCRIBE RELATIONSHIPS lists the relationship types
    When executing query:
      """
      CREATE (:Person {id: 1})-[:KNOWS {since: 2020}]->(:Person {id: 2})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE RELATIONSHIPS
      """
    Then the result should be, in any order:
      | relationshipType |
      | 'KNOWS'          |

  # The point of exposing the anonymous label: an unlabeled node's fields are
  # reachable by name. Note the space after the colon -- the TCK helper scans
  # queries for ":Name" and would otherwise auto-create a bogus label.
  Scenario: An unlabeled node's fields are reported under the anonymous label
    When executing query:
      """
      CREATE ({nickname: 'solo'})
      """
    Then the result should be empty
    When executing query:
      """
      DESCRIBE LABEL __anon__
      """
    Then the result should be, in any order:
      | label      | propertyName | propertyType |
      | '__anon__' | 'nickname'   | 'ANY'        |

  # `DESC` and `REL` are aliases; the quoted form carries a name with spaces.
  Scenario: DESCRIBE accepts the documented aliases
    When executing query:
      """
      CREATE (:Person {id: 1})-[:KNOWS {since: 2020}]->(:Person {id: 2})
      """
    Then the result should be empty
    When executing query:
      """
      DESC REL KNOWS
      """
    Then the result should be, in any order:
      | relType | propertyName | propertyType |
      | 'KNOWS' | 'since'        | 'INTEGER'    |

  Scenario: DESCRIBING an unknown label yields an empty result rather than an error
    When executing query:
      """
      DESCRIBE LABEL NoSuchLabel
      """
    Then the result should be empty

  Scenario: DESCRIBING an unknown relationship type yields an empty result rather than an error
    When executing query:
      """
      DESCRIBE RELATIONSHIP NO_SUCH_TYPE
      """
    Then the result should be empty
