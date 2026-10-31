import { describe, expect, it } from 'vitest';
import { extractGraph, GRAPH_LIMITS } from './graph-data';

function node(id: string, label: string, properties: Record<string, unknown> = {}) {
  return { elementId: id, labels: [label], properties };
}

function relationship(id: string, type: string, start: string, end: string, properties: Record<string, unknown> = {}) {
  return { elementId: id, type, startNodeElementId: start, endNodeElementId: end, properties };
}

function record(values: Record<string, unknown>) {
  return { keys: Object.keys(values), get: (key: string) => values[key] };
}

describe('extractGraph', () => {
  it('extracts nodes and relationships and deduplicates them', () => {
    const alice = node('n1', 'Person', { name: 'Alice' });
    const bob = node('n2', 'Person', { name: 'Bob' });
    const knows = relationship('r1', 'KNOWS', 'n1', 'n2');

    const result = extractGraph([
      record({ a: alice, r: knows, b: bob }),
      record({ a: alice, r: knows }),
    ]);

    expect(result.graph.order).toBe(2);
    expect(result.graph.size).toBe(1);
    expect(result.graph.hasNode('n1')).toBe(true);
    expect(result.graph.getNodeAttribute('n1', 'label')).toBe('Alice');
    expect(result.graph.getEdgeAttribute('r1', 'type')).toBe('KNOWS');
    expect(result.truncated).toBe(false);
  });

  it('creates placeholder nodes when only relationships are returned', () => {
    const result = extractGraph([record({ r: relationship('r1', 'KNOWS', 'n1', 'n2') })]);
    expect(result.graph.order).toBe(2);
    expect(result.graph.size).toBe(1);
    expect(result.graph.getNodeAttribute('n1', 'placeholder')).toBe(true);
  });

  it('expands paths', () => {
    const path = {
      start: node('n1', 'Person', { name: 'Alice' }),
      end: node('n2', 'Person', { name: 'Bob' }),
      segments: [
        {
          start: node('n1', 'Person', { name: 'Alice' }),
          end: node('n2', 'Person', { name: 'Bob' }),
          relationship: relationship('r1', 'KNOWS', 'n1', 'n2'),
        },
      ],
    };
    const result = extractGraph([record({ p: path })]);
    expect(result.graph.order).toBe(2);
    expect(result.graph.size).toBe(1);
  });

  it('keeps parallel edges and self-loops', () => {
    const n1 = node('n1', 'Person');
    const n2 = node('n2', 'Person');
    const result = extractGraph([
      record({
        a: n1,
        b: n2,
        r1: relationship('r1', 'KNOWS', 'n1', 'n2'),
        r2: relationship('r2', 'LIKES', 'n1', 'n2'),
        loop: relationship('r3', 'SELF', 'n1', 'n1'),
      }),
    ]);
    expect(result.graph.size).toBe(3);
    expect(result.graph.multi).toBe(true);
  });

  it('caps the graph and reports truncation', () => {
    const records = [];
    for (let index = 0; index <= GRAPH_LIMITS.nodes; index += 1) {
      records.push(record({ n: node(`n${index}`, 'Bulk') }));
    }
    const result = extractGraph(records);
    expect(result.graph.order).toBe(GRAPH_LIMITS.nodes);
    expect(result.truncated).toBe(true);
  });
});

describe('generic node captions', () => {
  it('uses name-suffix properties without a Person-specific field list', () => {
    const person = node('n1', 'Person', { firstName: 'Mahinda', lastName: 'Perera', id: 933 });
    const result = extractGraph([record({ n: person })]);
    expect(result.graph.getNodeAttribute('n1', 'caption')).toBe('Mahinda');
  });

  it('falls back to description and then any scalar property', () => {
    const withDescription = node('n1', 'Thing', { description: 'A thing', id: 1 });
    const withScalar = node('n2', 'Thing', { code: 'X-1', id: 2 });
    const result = extractGraph([record({ a: withDescription, b: withScalar })]);
    expect(result.graph.getNodeAttribute('n1', 'caption')).toBe('A thing');
    expect(result.graph.getNodeAttribute('n2', 'caption')).toBe('X-1');
  });
});

describe('component center highlighting', () => {
  it('highlights the highest-degree node in a component', () => {
    const center = node('n1', 'Person', { name: 'Center' });
    const a = node('n2', 'Person', { name: 'A' });
    const b = node('n3', 'Person', { name: 'B' });
    const c = node('n4', 'Person', { name: 'C' });
    const result = extractGraph([
      record({
        a: center,
        r1: relationship('r1', 'KNOWS', 'n1', 'n2'),
        r2: relationship('r2', 'KNOWS', 'n1', 'n3'),
        r3: relationship('r3', 'KNOWS', 'n1', 'n4'),
      }),
    ]);
    expect(result.graph.getNodeAttribute('n1', 'hub')).toBe(true);
    expect(result.graph.getNodeAttribute('n1', 'color')).not.toBe(
      result.graph.getNodeAttribute('n2', 'color')
    );
  });
});
