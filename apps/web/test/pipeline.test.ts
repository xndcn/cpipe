import { describe, expect, it } from "vitest";

import fullClassicPipeline from "../../../examples/pipelines/full-classic-pipeline.cpipe.json";
import { pipelineToFlow } from "../src/store/pipeline";

describe("pipelineToFlow", () => {
  it("maps the current full-classic pipeline into React Flow nodes and edges", () => {
    const graph = pipelineToFlow(fullClassicPipeline);

    expect(graph.nodes).toHaveLength(15);
    expect(graph.edges).toHaveLength(14);
    expect(graph.nodes[0]).toMatchObject({
      id: "qbc",
      type: "cpipeNode",
      position: { x: 0, y: 0 }
    });
    expect(graph.nodes[1].position.x).toBeGreaterThan(graph.nodes[0].position.x);
  });

  it("uses declared ui coordinates when a node provides them", () => {
    const graph = pipelineToFlow({
      version: "0.4",
      nodes: [
        {
          id: "tone",
          type: "com.cpipe.tone.filmic_rgb",
          params: {},
          ui: { x: 320, y: 180 }
        }
      ],
      edges: []
    });

    expect(graph.nodes[0].position).toEqual({ x: 320, y: 180 });
  });
});
