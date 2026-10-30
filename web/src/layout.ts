import { MultiDirectedGraph } from 'graphology';
import {
  forceCollide,
  forceLink,
  forceManyBody,
  forceSimulation,
  forceX,
  forceY,
  type Simulation,
  type SimulationLinkDatum,
  type SimulationNodeDatum,
} from 'd3-force';

type LayoutNode = SimulationNodeDatum & {
  id: string;
  radius: number;
};

type LayoutLink = SimulationLinkDatum<LayoutNode>;

export type LiveGraphLayout = {
  simulation: Simulation<LayoutNode, undefined>;
  nodes: Map<string, LayoutNode>;
  syncGraph: () => void;
  stop: () => void;
};

const NODE_GAP = 18;
const LINK_GAP = 45;
const CHARGE = -400;
const CENTER_STRENGTH = 0.03;
const COLLIDE_PADDING = 25;
const VELOCITY_DECAY = 0.4;
const ALPHA_MIN = 0.05;
const PRECOMPUTE_TICKS = 300;

function nodeRadius(graph: MultiDirectedGraph, node: string): number {
  return (graph.getNodeAttribute(node, 'size') as number) ?? 10;
}

/** Nodes-only results are laid out as deterministic concentric rings. */
function layoutRing(graph: MultiDirectedGraph, nodes: string[]): void {
  if (nodes.length === 0) return;
  if (nodes.length === 1) {
    graph.setNodeAttribute(nodes[0], 'x', 0);
    graph.setNodeAttribute(nodes[0], 'y', 0);
    return;
  }

  const maxRadius = Math.max(...nodes.map((node) => nodeRadius(graph, node)));
  const spacing = 2 * maxRadius + NODE_GAP;
  const firstRingRadius = 2 * maxRadius + NODE_GAP;

  if (nodes.length <= 18) {
    const radius = Math.max(firstRingRadius, (nodes.length * spacing) / (2 * Math.PI));
    nodes.forEach((node, index) => {
      const angle = (2 * Math.PI * index) / nodes.length - Math.PI / 2;
      graph.setNodeAttribute(node, 'x', Math.cos(angle) * radius);
      graph.setNodeAttribute(node, 'y', Math.sin(angle) * radius);
    });
    return;
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
      graph.setNodeAttribute(nodes[index], 'x', Math.cos(angle) * radius);
      graph.setNodeAttribute(nodes[index], 'y', Math.sin(angle) * radius);
      index += 1;
    }
    remaining -= count;
    ring += 1;
  }
}

/**
 * Neo4j Browser's exact graph layout recipe:
 * initial circle -> d3-force (charge -400, center 0.03, link r1+r2+90,
 * collide r+25, velocityDecay 0.4), precomputed for up to 300 ticks.
 */
export function layoutGraph(graph: MultiDirectedGraph): void {
  prepareGraphLayout(graph)?.stop();
}

/**
 * Same layout as layoutGraph, but returns the stopped d3 simulation so the
 * caller can pin a node while dragging and restart it to let the graph react.
 */
export function prepareGraphLayout(graph: MultiDirectedGraph): LiveGraphLayout | null {
  const nodes = graph.nodes();
  if (nodes.length === 0) return null;
  if (graph.size === 0) {
    layoutRing(graph, nodes);
    return null;
  }

  const initialRadius = (nodes.length * LINK_GAP) / (Math.PI * 2);
  const simulationNodes: LayoutNode[] = nodes.map((node, index) => {
    const angle = (2 * Math.PI * index) / nodes.length;
    return {
      id: node,
      radius: nodeRadius(graph, node),
      x: Math.cos(angle) * initialRadius,
      y: Math.sin(angle) * initialRadius,
      vx: 0,
      vy: 0,
    };
  });

  // Neo4j collapses parallel/reciprocal relationships to one link per pair
  // before feeding them to forceLink, so duplicated links do not over-attract.
  const seenPairs = new Set<string>();
  const links: LayoutLink[] = [];
  graph.forEachEdge((_edge, _attrs, source, target) => {
    if (source === target) return;
    const key = source < target ? `${source}\u0000${target}` : `${target}\u0000${source}`;
    if (seenPairs.has(key)) return;
    seenPairs.add(key);
    links.push({ source, target });
  });

  if (simulationNodes.length === 1) {
    graph.setNodeAttribute(nodes[0], 'x', 0);
    graph.setNodeAttribute(nodes[0], 'y', 0);
    return null;
  }

  const linkForce = forceLink<LayoutNode, LayoutLink>(links)
    .id((node) => node.id)
    .distance((link) => {
      const source = link.source as LayoutNode;
      const target = link.target as LayoutNode;
      return source.radius + target.radius + LINK_GAP * 2;
    });

  const simulation = forceSimulation<LayoutNode>(simulationNodes)
    .velocityDecay(VELOCITY_DECAY)
    .force('charge', forceManyBody<LayoutNode>().strength(CHARGE))
    .force('centerX', forceX<LayoutNode>(0).strength(CENTER_STRENGTH))
    .force('centerY', forceY<LayoutNode>(0).strength(CENTER_STRENGTH))
    .force('collide', forceCollide<LayoutNode>((node) => node.radius + COLLIDE_PADDING))
    .force('link', linkForce)
    .alphaMin(ALPHA_MIN)
    .stop();

  simulation.tick(PRECOMPUTE_TICKS);

  const nodesById = new Map(simulationNodes.map((node) => [node.id, node]));
  const syncGraph = (): void => {
    for (const node of simulationNodes) {
      graph.setNodeAttribute(node.id, 'x', node.x ?? 0);
      graph.setNodeAttribute(node.id, 'y', node.y ?? 0);
    }
  };
  syncGraph();

  return {
    simulation,
    nodes: nodesById,
    syncGraph,
    stop: () => simulation.stop(),
  };
}
