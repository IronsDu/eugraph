import { describe, expect, it } from 'vitest';
import { formatValue, recordsToCsv, toPlainValue } from './format';

function record(values: Record<string, unknown>) {
  return {
    keys: Object.keys(values),
    get: (key: string) => values[key],
  };
}

describe('formatValue', () => {
  it('formats scalars and null', () => {
    expect(formatValue(null)).toBe('null');
    expect(formatValue(undefined)).toBe('null');
    expect(formatValue(42)).toBe('42');
    expect(formatValue(true)).toBe('true');
    expect(formatValue('hello')).toBe('hello');
  });

  it('formats nodes and relationships with their properties', () => {
    expect(
      formatValue({
        labels: ['Person', 'Employee'],
        properties: { name: 'Alice', age: 30 },
        elementId: 'n1',
      })
    ).toBe('(:Person:Employee {name: "Alice", age: 30})');
    expect(
      formatValue({
        type: 'KNOWS',
        properties: {},
        elementId: 'r1',
        startNodeElementId: 'n1',
        endNodeElementId: 'n2',
      })
    ).toBe('[:KNOWS]');
    expect(
      formatValue({
        type: 'KNOWS',
        properties: { since: 2019 },
        elementId: 'r2',
        startNodeElementId: 'n1',
        endNodeElementId: 'n2',
      })
    ).toBe('[:KNOWS {since: 2019}]');
  });

  it('quotes property strings but keeps top-level strings readable', () => {
    expect(formatValue('hello')).toBe('hello');
    expect(formatValue(['a', 'b'])).toBe('[a, b]');
    expect(
      formatValue({
        labels: ['Person'],
        properties: { name: 'Alice', tags: ['engineer', 'speaker'] },
        elementId: 'n1',
      })
    ).toBe('(:Person {name: "Alice", tags: ["engineer", "speaker"]})');
  });

  it('puts name/title/id first in property summaries', () => {
    expect(
      formatValue({
        labels: ['Person'],
        properties: { role: 'Engineer', age: 30, name: 'Alice', city: 'Shanghai' },
        elementId: 'n1',
      })
    ).toBe('(:Person {name: "Alice", age: 30, city: "Shanghai", role: "Engineer"})');
  });

  it('produces a JSON-able inspector representation', () => {
    expect(
      toPlainValue({
        labels: ['Person'],
        properties: { name: 'Alice' },
        elementId: 'n1',
      })
    ).toEqual({ kind: 'node', elementId: 'n1', labels: ['Person'], properties: { name: 'Alice' } });
  });

  it('formats lists and maps', () => {
    expect(formatValue([1, 'a', null])).toBe('[1, a, null]');
    expect(formatValue({ nested: [1, 2] })).toBe('{"nested":[1,2]}');
  });
});

describe('recordsToCsv', () => {
  it('escapes commas, quotes and newlines', () => {
    const rows = [record({ name: 'a,b', note: 'say "hi"', text: 'x\ny' })];
    expect(recordsToCsv(['name', 'note', 'text'], rows)).toBe('name,note,text\n"a,b","say ""hi""","x\ny"\n');
  });

  it('writes a header even for an empty result', () => {
    expect(recordsToCsv(['a', 'b'], [])).toBe('a,b\n');
  });
});
