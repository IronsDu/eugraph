import { describe, expect, it } from 'vitest';
import { completionsFor, type CompletionSchema } from './autocomplete';

const schema: CompletionSchema = {
  labels: ['Person', 'City'],
  relationships: ['KNOWS', 'CONTAINER_OF'],
  properties: ['name', 'firstName', 'birthday'],
  labelProperties: {
    Person: [
      { name: 'name', type: 'STRING' },
      { name: 'firstName', type: 'STRING' },
      { name: 'birthday', type: 'INTEGER' },
    ],
    City: [{ name: 'name', type: 'STRING' }],
  },
  relationshipProperties: {
    KNOWS: [{ name: 'creationDate', type: 'INTEGER' }],
  },
  procedures: [{ name: 'db.labels', detail: 'db.labels() :: (label :: STRING)' }],
};

describe('Cypher completions', () => {
  it('suggests clause keywords from a one-letter prefix', () => {
    const result = completionsFor('m', 1, schema);
    expect(result.start).toBe(0);
    expect(result.end).toBe(1);
    expect(result.items.some((item) => item.text === 'MATCH')).toBe(true);
    expect(result.items.some((item) => item.text === 'MERGE')).toBe(true);
    expect(result.items.find((item) => item.text === 'MATCH')?.insert).toBe('MATCH ');
  });

  it('suggests labels after a node colon', () => {
    const query = 'MATCH (n:Per';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('label');
    expect(result.items.map((item) => item.text)).toContain('Person');
    expect(result.items.every((item) => item.kind === 'label')).toBe(true);
  });

  it('suggests relationship types after a bracket colon', () => {
    const query = 'MATCH (a)-[r:KN';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('relationship');
    expect(result.items.map((item) => item.text)).toContain('KNOWS');
  });

  it('suggests property keys after a dot', () => {
    const query = 'MATCH (n) RETURN n.';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('property');
    expect(result.items.map((item) => item.text)).toContain('firstName');
    expect(result.start).toBe(query.length);
    expect(result.end).toBe(query.length);
  });

  it('offers functions with a caret inside the parentheses', () => {
    const result = completionsFor('RETURN cou', 10, schema);
    const count = result.items.find((item) => item.text === 'count');
    expect(count).toBeDefined();
    expect(count?.insert).toBe('count()');
    expect(count?.caretInside).toBe(6);
  });
});


describe('EuGraph-specific completions', () => {
  it('suggests labels after ::', () => {
    const query = 'RETURN n::Per';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('label');
    expect(result.items.map((item) => item.text)).toContain('Person');
  });

  it('suggests typed properties after ::Label.', () => {
    const query = 'RETURN n::Person.';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('property');
    expect(result.items.map((item) => item.text)).toContain('birthday');
    expect(result.items.find((item) => item.text === 'birthday')?.detail).toBe('INTEGER');
  });

  it('infers the label of a node variable for property completion', () => {
    const query = 'MATCH (n:Person) RETURN n.na';
    const result = completionsFor(query, query.length, schema);
    expect(result.context).toBe('property');
    expect(result.items.map((item) => item.text)).toContain('name');
    expect(result.items.some((item) => item.text === 'firstName')).toBe(true);
  });

  it('completes DESCRIBE LABEL and DESCRIBE RELATIONSHIP targets', () => {
    const labels = 'DESCRIBE LABEL Per';
    const labelResult = completionsFor(labels, labels.length, schema);
    expect(labelResult.items.map((item) => item.text)).toContain('Person');

    const relationships = 'DESCRIBE RELATIONSHIP KN';
    const relationshipResult = completionsFor(relationships, relationships.length, schema);
    expect(relationshipResult.items.map((item) => item.text)).toContain('KNOWS');
  });

  it('completes CALL procedures and SHOW keywords', () => {
    const call = 'CALL db.la';
    const callResult = completionsFor(call, call.length, schema);
    expect(callResult.context).toBe('procedure');
    expect(callResult.items[0]?.text).toBe('db.labels');
    expect(callResult.items[0]?.insert).toBe('db.labels()');

    const show = 'SHOW I';
    const showResult = completionsFor(show, show.length, schema);
    expect(showResult.items.map((item) => item.text)).toContain('INDEXES');
  });
});
