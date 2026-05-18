import {
  Background,
  Controls,
  MiniMap,
  ReactFlow,
  ReactFlowProvider,
  type NodeProps,
  type NodeTypes
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import { type ComponentType, useMemo } from "react";

import { type CpipeFlowNode, usePipelineStore } from "../store/pipeline";
import { BaseNode } from "./nodes/BaseNode";

const nodeTypes: NodeTypes = {
  cpipeNode: BaseNode as ComponentType<NodeProps<CpipeFlowNode>>
};

export function FlowCanvas() {
  const edges = usePipelineStore((state) => state.edges);
  const nodes = usePipelineStore((state) => state.nodes);
  const selectedId = usePipelineStore((state) => state.selectedId);
  const setSelectedId = usePipelineStore((state) => state.setSelectedId);

  const selectedNodes = useMemo(
    () => nodes.map((node) => ({ ...node, selected: node.id === selectedId })),
    [nodes, selectedId]
  );

  return (
    <ReactFlowProvider>
      <ReactFlow
        colorMode="light"
        edges={edges}
        fitView
        minZoom={0.2}
        nodes={selectedNodes}
        nodeTypes={nodeTypes}
        onNodeClick={(_, node) => setSelectedId(node.id)}
        proOptions={{ hideAttribution: true }}
      >
        <Background gap={24} />
        <MiniMap pannable zoomable />
        <Controls position="bottom-right" />
      </ReactFlow>
    </ReactFlowProvider>
  );
}
