import { describe, expect, it } from 'vitest';
import { completionsFor, type CompletionSchema } from './autocomplete';

const schema: CompletionSchema = {
  labels: ['Person', 'City'],
  relationships: ['KNOWS', 'CONTAINER_OF'],
  properties: ['name', 'firstName', 'birthday'],
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
