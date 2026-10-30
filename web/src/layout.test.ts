import { describe, expect, it } from 'vitest';
import { MultiDirectedGraph } from 'graphology';
import { layoutGraph } from './layout';

function addNode(graph: MultiDirectedGraph, id: string) {
  graph.addNode(id, { size: 12, label: id });
}

function distance(graph: MultiDirectedGraph, a: string, b: string): number {
  return Math.hypot(
    graph.getNodeAttribute(a, 'x') - graph.getNodeAttribute(b, 'x'),
    graph.getNodeAttribute(a, 'y') - graph.getNodeAttribute(b, 'y')
  );
}

describe('layoutGraph', () => {
  it('arranges nodes without edges on a regular ring around the center', () => {
    const graph = new MultiDirectedGraph();
    for (let i = 0; i < 11; i += 1) addNode(graph, `n${i}`);
    layoutGraph(graph);

    const radii = graph.nodes().map((node) => Math.hypot(graph.getNodeAttribute(node, 'x'), graph.getNodeAttribute(node, 'y')));
    const average = radii.reduce((sum, value) => sum + value, 0) / radii.length;
    expect(average).toBeGreaterThan(0);
    for (const radius of radii) expect(radius).toBeCloseTo(average, 5);
    expect(new Set(graph.nodes().map((n) => `${graph.getNodeAttribute(n, 'x')},${graph.getNodeAttribute(n, 'y')}`)).size).toBe(11);
  });

  it('lays out disconnected components separately', () => {
    const graph = new MultiDirectedGraph();
    for (const id of ['a1', 'a2', 'b1', 'b2']) addNode(graph, id);
    graph.addEdge('a1', 'a2');
    graph.addEdge('b1', 'b2');
    layoutGraph(graph);

    const a = graph.getNodeAttribute('a1', 'x') + graph.getNodeAttribute('a2', 'x');
    const b = graph.getNodeAttribute('b1', 'x') + graph.getNodeAttribute('b2', 'x');
    expect(Math.abs(a / 2 - b / 2)).toBeGreaterThan(20);
    expect(distance(graph, 'a1', 'b1')).toBeGreaterThan(20);
  });

  it('keeps a single connected component finite and non-degenerate', () => {
    const graph = new MultiDirectedGraph();
    for (const id of ['a', 'b', 'c', 'd']) addNode(graph, id);
    graph.addEdge('a', 'b');
    graph.addEdge('b', 'c');
    graph.addEdge('c', 'd');
    layoutGraph(graph);
    for (const node of graph.nodes()) {
      expect(Number.isFinite(graph.getNodeAttribute(node, 'x'))).toBe(true);
      expect(Number.isFinite(graph.getNodeAttribute(node, 'y'))).toBe(true);
    }
    expect(distance(graph, 'a', 'd')).toBeGreaterThan(0);
  });
});
