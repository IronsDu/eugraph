import { MultiDirectedGraph } from 'graphology';
import forceAtlas2 from 'graphology-layout-forceatlas2';

type Positioned = { nodes: string[]; radius: number };

const NODE_MARGIN = 18;
const COMPONENT_GAP = 56;
const NODE_GAP = 18;
const LINK_DISTANCE = 45;

/** Deterministic concentric-ring placement around (0, 0). */
function ringPositions(
  nodes: string[],
  graph: MultiDirectedGraph
): Map<string, { x: number; y: number }> {
  const positions = new Map<string, { x: number; y: number }>();
  if (nodes.length === 0) return positions;
  if (nodes.length === 1) {
    positions.set(nodes[0], { x: 0, y: 0 });
    return positions;
  }

  const maxRadius = Math.max(...nodes.map((node) => (graph.getNodeAttribute(node, 'size') as number) ?? 10));
  // Sigma node `size` is a radius in graph units. Leave a real gap between
  // neighboring circles instead of scaling the text/circle independently.
  const spacing = 2 * maxRadius + NODE_GAP;
  const firstRingRadius = 2 * maxRadius + NODE_GAP;

  // Small graphs look best on one ring; larger ones use concentric rings.
  if (nodes.length <= 18) {
    const radius = Math.max(firstRingRadius, (nodes.length * spacing) / (2 * Math.PI));
    nodes.forEach((node, index) => {
      const angle = (2 * Math.PI * index) / nodes.length - Math.PI / 2;
      positions.set(node, { x: Math.cos(angle) * radius, y: Math.sin(angle) * radius });
    });
    return positions;
  }

  let remaining = nodes.length;
  let index = 0;
  let ring = 0;
  while (remaining > 0) {
    const radius = firstRingRadius + ring * spacing;
    const capacity = Math.max(6, Math.floor((2 * Math.PI * radius) / spacing));
    const count = Math.min(remaining, capacity);
    for (let i = 0; i < count; i += 1) {
      const angle = (2 * Math.PI * i) / count - Math.PI / 2 + (ring % 2) * (Math.PI / count);
      positions.set(nodes[index], { x: Math.cos(angle) * radius, y: Math.sin(angle) * radius });
      index += 1;
    }
    remaining -= count;
    ring += 1;
  }
  return positions;
}

/** Undirected connected components over the graph's edges. */
function connectedComponents(graph: MultiDirectedGraph): { components: string[][]; isolated: string[] } {
  const parent = new Map<string, string>();
  const find = (node: string): string => {
    let root = node;
    while (parent.get(root) !== root) root = parent.get(root)!;
    while (parent.get(node) !== root) {
      const next = parent.get(node)!;
      parent.set(node, root);
      node = next;
    }
    return root;
  };
  const union = (a: string, b: string): void => {
    const rootA = find(a);
    const rootB = find(b);
    if (rootA !== rootB) parent.set(rootA, rootB);
  };

  for (const node of graph.nodes()) parent.set(node, node);
  const incident = new Set<string>();
  graph.forEachEdge((_edge, _attrs, source, target) => {
    incident.add(source);
    incident.add(target);
    union(source, target);
  });

  const groups = new Map<string, string[]>();
  for (const node of graph.nodes()) {
    if (!incident.has(node)) continue;
    const root = find(node);
    const group = groups.get(root);
    if (group) group.push(node);
    else groups.set(root, [node]);
  }
  const isolated = graph.nodes().filter((node) => !incident.has(node));
  return { components: Array.from(groups.values()), isolated };
}

function componentSubgraph(graph: MultiDirectedGraph, nodes: string[]): MultiDirectedGraph {
  const sub = new MultiDirectedGraph();
  const member = new Set(nodes);
  const radius = Math.max(10, Math.sqrt(nodes.length) * 40);
  nodes.forEach((node, index) => {
    const angle = (2 * Math.PI * index) / nodes.length - Math.PI / 2;
    sub.addNode(node, {
      x: Math.cos(angle) * radius,
      y: Math.sin(angle) * radius,
      size: graph.getNodeAttribute(node, 'size') ?? 10,
    });
  });
  graph.forEachEdge((edge, _attrs, source, target) => {
    if (member.has(source) && member.has(target)) {
      sub.addEdgeWithKey(edge, source, target);
    }
  });
  return sub;
}

function layoutComponentForce(
  graph: MultiDirectedGraph,
  nodes: string[],
  sub: MultiDirectedGraph
): Positioned {
  const index = new Map<string, number>();
  const x = new Float64Array(nodes.length);
  const y = new Float64Array(nodes.length);
  const vx = new Float64Array(nodes.length);
  const vy = new Float64Array(nodes.length);
  const radii = new Float64Array(nodes.length);
  let maxNodeRadius = 0;
  nodes.forEach((node, i) => {
    index.set(node, i);
    x[i] = (sub.getNodeAttribute(node, 'x') as number) ?? 0;
    y[i] = (sub.getNodeAttribute(node, 'y') as number) ?? 0;
    radii[i] = (graph.getNodeAttribute(node, 'size') as number) ?? 10;
    maxNodeRadius = Math.max(maxNodeRadius, radii[i]);
  });

  const links: Array<{ source: number; target: number }> = [];
  const degree = new Int32Array(nodes.length);
  sub.forEachEdge((_edge, _attrs, source, target) => {
    const a = index.get(source);
    const b = index.get(target);
    if (a === undefined || b === undefined || a === b) return;
    links.push({ source: a, target: b });
    degree[a] += 1;
    degree[b] += 1;
  });

  let alpha = 1;
  let tick = 0;
  while (alpha > 0.05 && tick < 300) {
    tick += 1;
    alpha *= 0.985;

    // Charge and collision, Neo4j forceManyBody(-400) / forceCollide(r+25).
    for (let i = 0; i < nodes.length; i += 1) {
      for (let j = i + 1; j < nodes.length; j += 1) {
        let dx = x[j] - x[i];
        let dy = y[j] - y[i];
        let distance = Math.hypot(dx, dy);
        if (distance < 0.001) {
          dx = 0.5;
          dy = 0.5;
          distance = Math.hypot(dx, dy);
        }
        const charge = Math.min(2, (400 * alpha) / Math.max(1, distance * distance));
        vx[i] -= (dx / distance) * charge;
        vy[i] -= (dy / distance) * charge;
        vx[j] += (dx / distance) * charge;
        vy[j] += (dy / distance) * charge;

        const minimum = radii[i] + radii[j] + 25;
        if (distance < minimum) {
          const push = Math.min(30, (minimum - distance) * 0.5) / distance;
          const px = dx * push;
          const py = dy * push;
          x[i] -= px;
          y[i] -= py;
          x[j] += px;
          y[j] += py;
        }
      }
    }

    // Links, target distance r1 + r2 + 90.
    for (const link of links) {
      const a = link.source;
      const b = link.target;
      const dx = x[b] - x[a];
      const dy = y[b] - y[a];
      const distance = Math.hypot(dx, dy) || 0.001;
      const target = radii[a] + radii[b] + 90;
      const strength = 1 / Math.max(1, Math.min(degree[a], degree[b]));
      const force = Math.max(
        -1.5,
        Math.min(1.5, ((distance - target) / distance) * strength * alpha)
      );
      vx[a] += (dx / distance) * force;
      vy[a] += (dy / distance) * force;
      vx[b] -= (dx / distance) * force;
      vy[b] -= (dy / distance) * force;
    }

    // Center force (0.03) and velocity integration.
    for (let i = 0; i < nodes.length; i += 1) {
      vx[i] += -x[i] * 0.03 * alpha;
      vy[i] += -y[i] * 0.03 * alpha;
      vx[i] = Math.max(-20, Math.min(20, vx[i]));
      vy[i] = Math.max(-20, Math.min(20, vy[i]));
      vx[i] *= 0.6;
      vy[i] *= 0.6;
      x[i] = Math.max(-1e5, Math.min(1e5, x[i] + vx[i]));
      y[i] = Math.max(-1e5, Math.min(1e5, y[i] + vy[i]));
    }
  }

  let sumX = 0;
  let sumY = 0;
  for (let i = 0; i < nodes.length; i += 1) {
    sumX += x[i];
    sumY += y[i];
  }
  const centerX = sumX / nodes.length;
  const centerY = sumY / nodes.length;
  let sumNodeRadius = 0;
  for (let i = 0; i < nodes.length; i += 1) sumNodeRadius += radii[i];
  const averageNodeRadius = sumNodeRadius / nodes.length;
  const packing =
    nodes.length <= 20 ? 0.85 : nodes.length <= 100 ? 0.75 : nodes.length <= 300 ? 0.95 : 1.0;
  const targetRadius = Math.max(
    80,
    Math.sqrt(nodes.length) * (2 * averageNodeRadius + NODE_GAP) * packing
  );
  let maxDistance = 0;
  for (let i = 0; i < nodes.length; i += 1) {
    maxDistance = Math.max(maxDistance, Math.hypot(x[i] - centerX, y[i] - centerY));
  }
  const scale = targetRadius / Math.max(1, maxDistance);
  let radius = 0;
  nodes.forEach((node, i) => {
    const px = (x[i] - centerX) * scale;
    const py = (y[i] - centerY) * scale;
    graph.setNodeAttribute(node, 'x', px);
    graph.setNodeAttribute(node, 'y', py);
    radius = Math.max(radius, Math.hypot(px, py));
  });
  return { nodes, radius: Math.max(NODE_MARGIN, radius + maxNodeRadius + NODE_MARGIN) };
}

function layoutComponent(graph: MultiDirectedGraph, nodes: string[]): Positioned {
  if (nodes.length === 1) {
    graph.setNodeAttribute(nodes[0], 'x', 0);
    graph.setNodeAttribute(nodes[0], 'y', 0);
    return { nodes, radius: NODE_MARGIN };
  }

  const sub = componentSubgraph(graph, nodes);
  if (nodes.length <= 600) {
    return layoutComponentForce(graph, nodes, sub);
  }
  const iterations = nodes.length <= 80 ? 220 : nodes.length <= 400 ? 160 : 110;
  try {
    forceAtlas2.assign(sub, {
      iterations,
      settings: {
        barnesHutOptimize: nodes.length > 80,
        adjustSizes: true,
        slowDown: 3,
        gravity: 0.2,
        scalingRatio: nodes.length > 100 ? 12 : 4,
        edgeWeightInfluence: 0.2,
      },
    });
  } catch (error) {
    console.warn('ForceAtlas2 component layout failed', error);
  }

  const positions = new Map<string, { x: number; y: number }>();
  const radii = new Map<string, number>();
  let sumNodeRadius = 0;
  let maxNodeRadius = 0;
  for (const node of nodes) {
    const radius = (graph.getNodeAttribute(node, 'size') as number) ?? 10;
    radii.set(node, radius);
    sumNodeRadius += radius;
    maxNodeRadius = Math.max(maxNodeRadius, radius);
    positions.set(node, {
      x: (sub.getNodeAttribute(node, 'x') as number) ?? 0,
      y: (sub.getNodeAttribute(node, 'y') as number) ?? 0,
    });
  }

  // Neo4j-style link/collide relaxation: force long links (r1+r2+90) and
  // keep node circles apart (r1+r2+25). This spreads a dense component much
  // better than pure ForceAtlas2 edge attraction.
  const links: { source: string; target: string }[] = [];
  sub.forEachEdge((_edge, _attrs, source, target) => {
    if (source !== target) links.push({ source, target });
  });
  for (let iteration = 0; iteration < 18; iteration += 1) {
    const moves = new Map<string, { x: number; y: number }>();
    for (const node of nodes) moves.set(node, { x: 0, y: 0 });
    const addMove = (node: string, x: number, y: number): void => {
      const move = moves.get(node);
      if (move) {
        move.x += x;
        move.y += y;
      }
    };

    for (const link of links) {
      const a = positions.get(link.source);
      const b = positions.get(link.target);
      if (!a || !b) continue;
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const distance = Math.hypot(dx, dy);
      if (distance < 0.001) continue;
      const target =
        (radii.get(link.source) ?? 10) + (radii.get(link.target) ?? 10) + LINK_DISTANCE * 2;
      if (distance < target) {
        const push = (target - distance) * 0.3;
        addMove(link.source, (-dx / distance) * push, (-dy / distance) * push);
        addMove(link.target, (dx / distance) * push, (dy / distance) * push);
      }
    }

    if (nodes.length <= 600) {
      for (let i = 0; i < nodes.length; i += 1) {
        const a = positions.get(nodes[i]);
        if (!a) continue;
        for (let j = i + 1; j < nodes.length; j += 1) {
          const b = positions.get(nodes[j]);
          if (!b) continue;
          const dx = b.x - a.x;
          const dy = b.y - a.y;
          const distance = Math.hypot(dx, dy);
          const minimum = (radii.get(nodes[i]) ?? 10) + (radii.get(nodes[j]) ?? 10) + 25;
          if (distance < minimum) {
            if (distance < 0.001) {
              addMove(nodes[i], -0.5, 0);
              addMove(nodes[j], 0.5, 0);
            } else {
              const push = (minimum - distance) * 0.35;
              addMove(nodes[i], (-dx / distance) * push, (-dy / distance) * push);
              addMove(nodes[j], (dx / distance) * push, (dy / distance) * push);
            }
          }
        }
      }
    }

    for (const [node, move] of moves) {
      const position = positions.get(node);
      if (position) {
        position.x += move.x;
        position.y += move.y;
      }
    }
  }

  let sumX = 0;
  let sumY = 0;
  for (const position of positions.values()) {
    sumX += position.x;
    sumY += position.y;
  }
  const centerX = sumX / nodes.length;
  const centerY = sumY / nodes.length;
  let centerOffsetX = 0;
  let centerOffsetY = 0;
  const relative = nodes.map((node) => {
    const position = positions.get(node)!;
    const x = position.x - centerX;
    const y = position.y - centerY;
    centerOffsetX += x;
    centerOffsetY += y;
    return { node, x, y };
  });
  centerOffsetX /= nodes.length;
  centerOffsetY /= nodes.length;

  const distances = relative
    .map((position) => Math.hypot(position.x - centerOffsetX, position.y - centerOffsetY))
    .sort((a, b) => a - b);
  const q95 = distances[Math.min(distances.length - 1, Math.floor((distances.length - 1) * 0.95))] ?? 1;
  const averageNodeRadius = sumNodeRadius / nodes.length;
  const packing =
    nodes.length <= 20 ? 0.85 : nodes.length <= 100 ? 0.75 : nodes.length <= 300 ? 0.95 : 1.0;
  const targetRadius = Math.max(
    80,
    Math.sqrt(nodes.length) * (2 * averageNodeRadius + NODE_GAP) * packing
  );
  const scale = targetRadius / Math.max(q95, 1);
  let radius = 0;
  for (const position of relative) {
    let x = (position.x - centerOffsetX) * scale;
    let y = (position.y - centerOffsetY) * scale;
    const distance = Math.hypot(x, y);
    if (distance > targetRadius) {
      const factor = targetRadius / distance;
      x *= factor;
      y *= factor;
    }
    graph.setNodeAttribute(position.node, 'x', x);
    graph.setNodeAttribute(position.node, 'y', y);
    radius = Math.max(radius, Math.hypot(x, y));
  }

  return {
    nodes,
    radius: Math.max(NODE_MARGIN, targetRadius + maxNodeRadius + NODE_MARGIN),
  };
}
/** Pack component blocks without a tiny component inflating the main extent. */
function packBlocks(graph: MultiDirectedGraph, blocks: Positioned[]): void {
  if (blocks.length === 0 || blocks.length === 1) return;

  const sorted = [...blocks].sort((a, b) => b.radius - a.radius);
  const translate = (block: Positioned, offsetX: number, offsetY: number): void => {
    for (const node of block.nodes) {
      const x = (graph.getNodeAttribute(node, 'x') as number) ?? 0;
      const y = (graph.getNodeAttribute(node, 'y') as number) ?? 0;
      graph.setNodeAttribute(node, 'x', x + offsetX);
      graph.setNodeAttribute(node, 'y', y + offsetY);
    }
  };

  // For a dominant component plus a few small ones, place the small ones
  // around the main component instead of a sparse max-radius grid.
  if (sorted.length <= 8) {
    const placed: { x: number; y: number; radius: number }[] = [{ x: 0, y: 0, radius: sorted[0].radius }];
    for (let index = 1; index < sorted.length; index += 1) {
      const block = sorted[index];
      let angle = index * 2.399963;
      let distance = sorted[0].radius + block.radius + COMPONENT_GAP;
      let x = 0;
      let y = 0;
      for (let attempt = 0; attempt < 120; attempt += 1) {
        x = Math.cos(angle) * distance;
        y = Math.sin(angle) * distance;
        const clear = placed.every(
          (other) => Math.hypot(x - other.x, y - other.y) >= block.radius + other.radius + COMPONENT_GAP
        );
        if (clear) break;
        angle += 0.55;
        distance += 14;
      }
      placed.push({ x, y, radius: block.radius });
      translate(block, x, y);
    }
    return;
  }

  const columns = Math.ceil(Math.sqrt(sorted.length));
  const rows = Math.ceil(sorted.length / columns);
  const maxRadius = Math.max(...sorted.map((block) => block.radius));
  const cell = 2 * maxRadius + COMPONENT_GAP;
  sorted.forEach((block, index) => {
    const column = index % columns;
    const row = Math.floor(index / columns);
    translate(block, (column - (columns - 1) / 2) * cell, (row - (rows - 1) / 2) * cell);
  });
}
/**
 * Neo4j-style layout:
 * - no relationships: deterministic concentric rings around the center;
 * - with relationships: per-connected-component ForceAtlas2, then components
 *   are packed on a grid so distinct groups do not overlap.
 */
export function layoutGraph(graph: MultiDirectedGraph): void {
  const nodes = graph.nodes();
  if (nodes.length === 0) return;

  if (graph.size === 0) {
    const positions = ringPositions(nodes, graph);
    for (const [node, position] of positions) {
      graph.setNodeAttribute(node, 'x', position.x);
      graph.setNodeAttribute(node, 'y', position.y);
    }
    return;
  }

  // Neo4j runs one force simulation over the whole result. Keeping all
  // components together avoids pushing small disconnected groups far away and
  // shrinking the main component when a query returns many tiny components.
  if (graph.order <= 600) {
    const allNodes = graph.nodes();
    const sub = componentSubgraph(graph, allNodes);
    layoutComponentForce(graph, allNodes, sub);
    return;
  }

  const { components, isolated } = connectedComponents(graph);
  const blocks: Positioned[] = [];

  // Isolated nodes are one visual group, arranged as a ring inside its block.
  if (isolated.length > 0) {
    const positions = ringPositions(isolated, graph);
    let ringRadius = 0;
    let maxNodeRadius = 0;
    for (const [node, position] of positions) {
      graph.setNodeAttribute(node, 'x', position.x);
      graph.setNodeAttribute(node, 'y', position.y);
      ringRadius = Math.max(ringRadius, Math.hypot(position.x, position.y));
      maxNodeRadius = Math.max(maxNodeRadius, (graph.getNodeAttribute(node, 'size') as number) ?? 10);
    }
    blocks.push({
      nodes: isolated,
      radius: Math.max(NODE_MARGIN, ringRadius + maxNodeRadius + NODE_MARGIN),
    });
  }

  for (const component of components) {
    blocks.push(layoutComponent(graph, component));
  }

  packBlocks(graph, blocks);

  // Guard against non-finite coordinates from a failed layout.
  for (const node of nodes) {
    const x = graph.getNodeAttribute(node, 'x');
    const y = graph.getNodeAttribute(node, 'y');
    if (typeof x !== 'number' || !Number.isFinite(x) || typeof y !== 'number' || !Number.isFinite(y)) {
      const index = nodes.indexOf(node);
      const angle = index * 2.399963;
      const radius = 24 + (index % 24) * 8;
      graph.setNodeAttribute(node, 'x', Math.cos(angle) * radius);
      graph.setNodeAttribute(node, 'y', Math.sin(angle) * radius);
    }
  }
}
