import { create } from "zustand";
import type { Edge, Node, Viewport } from "@xyflow/react";

export interface CpipePipelineNode {
  id: string;
  type: string;
  params?: Record<string, unknown>;
  ui?: {
    x?: number;
    y?: number;
    color?: string;
    collapsed?: boolean;
  };
}

export type CpipeEndpoint =
  | string
  | {
      node?: string;
      port?: string;
    };

export interface CpipePipelineEdge {
  from?: CpipeEndpoint;
  to?: CpipeEndpoint;
  source?: string;
  sourceHandle?: string;
  target?: string;
  targetHandle?: string;
}

export interface CpipePipeline {
  version: string;
  nodes: CpipePipelineNode[];
  edges: CpipePipelineEdge[];
}

export interface CpipePort {
  id: string;
  label: string;
  type: string;
}

export interface CpipeNodeData extends Record<string, unknown> {
  category: string;
  categoryColor: string;
  inputs: CpipePort[];
  label: string;
  nodeId: string;
  outputs: CpipePort[];
  typeName: string;
}

export type CpipeFlowNode = Node<CpipeNodeData, "cpipeNode">;
export type CpipeFlowEdge = Edge;

export interface CpipeFlowGraph {
  nodes: CpipeFlowNode[];
  edges: CpipeFlowEdge[];
}

export type PipelineStatus = "idle" | "loading" | "ready";

export interface PipelineState {
  edges: CpipeFlowEdge[];
  loadPipeline: (pipeline: CpipePipeline) => void;
  nodes: CpipeFlowNode[];
  selectedId: string | null;
  setSelectedId: (selectedId: string | null) => void;
  status: PipelineStatus;
  setStatus: (status: PipelineStatus) => void;
  setViewport: (viewport: Viewport) => void;
  viewport: Viewport;
}

const fallbackColumnWidth = 260;
const fallbackRowHeight = 170;
const fallbackColumns = 5;

const categoryColors: Record<string, string> = {
  blacklevel: "#475569",
  color: "#2563eb",
  colormatrix: "#0891b2",
  demosaic: "#16a34a",
  denoise: "#9333ea",
  fusion: "#dc2626",
  lens: "#ca8a04",
  linearize: "#0d9488",
  output: "#4f46e5",
  precision: "#64748b",
  sharpen: "#ea580c",
  tone: "#7c3aed",
  wb: "#0284c7"
};

function nodeCategory(typeName: string) {
  const parts = typeName.split(".");
  return parts.length >= 3 ? parts[2] : "node";
}

function nodeLabel(typeName: string) {
  const label = typeName.split(".").at(-1) ?? typeName;
  return label
    .split("_")
    .filter(Boolean)
    .map((part) => part[0].toUpperCase() + part.slice(1))
    .join(" ");
}

function fallbackPosition(index: number) {
  return {
    x: (index % fallbackColumns) * fallbackColumnWidth,
    y: Math.floor(index / fallbackColumns) * fallbackRowHeight
  };
}

function parseEndpoint(endpoint: CpipeEndpoint | undefined) {
  if (typeof endpoint === "string") {
    const [node = "", port = ""] = endpoint.split(".", 2);
    return { node, port };
  }
  return {
    node: endpoint?.node ?? "",
    port: endpoint?.port ?? ""
  };
}

function edgeEndpoint(edge: CpipePipelineEdge, side: "source" | "target") {
  if (side === "source") {
    const from = parseEndpoint(edge.from);
    return {
      node: edge.source ?? from.node,
      port: edge.sourceHandle ?? from.port
    };
  }
  const to = parseEndpoint(edge.to);
  return {
    node: edge.target ?? to.node,
    port: edge.targetHandle ?? to.port
  };
}

export function pipelineToFlow(pipeline: CpipePipeline): CpipeFlowGraph {
  const nodes = pipeline.nodes.map<CpipeFlowNode>((node, index) => {
    const category = nodeCategory(node.type);
    const fallback = fallbackPosition(index);
    return {
      id: node.id,
      type: "cpipeNode",
      position: {
        x: node.ui?.x ?? fallback.x,
        y: node.ui?.y ?? fallback.y
      },
      data: {
        category,
        categoryColor: node.ui?.color ?? categoryColors[category] ?? "#334155",
        inputs: [{ id: "in", label: "In", type: "image" }],
        label: nodeLabel(node.type),
        nodeId: node.id,
        outputs: [{ id: "out", label: "Out", type: "image" }],
        typeName: node.type
      }
    };
  });

  const edges = pipeline.edges.map<CpipeFlowEdge>((edge, index) => {
    const source = edgeEndpoint(edge, "source");
    const target = edgeEndpoint(edge, "target");
    return {
      id: `${source.node}:${source.port}->${target.node}:${target.port}:${index}`,
      source: source.node,
      sourceHandle: source.port,
      target: target.node,
      targetHandle: target.port
    };
  });

  return { nodes, edges };
}

export const usePipelineStore = create<PipelineState>((set) => ({
  edges: [],
  loadPipeline: (pipeline) => {
    const graph = pipelineToFlow(pipeline);
    set({ ...graph, status: "ready" });
  },
  nodes: [],
  selectedId: null,
  setSelectedId: (selectedId) => set({ selectedId }),
  status: "idle",
  setStatus: (status) => set({ status }),
  setViewport: (viewport) => set({ viewport }),
  viewport: { x: 0, y: 0, zoom: 1 }
}));
