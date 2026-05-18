import { ReactFlowProvider } from "@xyflow/react";
import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { BaseNode } from "../src/components/nodes/BaseNode";

describe("BaseNode", () => {
  it("renders category color, label, id tooltip, and placeholders", () => {
    const markup = renderToStaticMarkup(
      <ReactFlowProvider>
        <BaseNode
          data={{
            category: "tone",
            categoryColor: "#7c3aed",
            inputs: [{ id: "in", label: "RGB", type: "rgba16f" }],
            label: "Filmic RGB",
            nodeId: "tone",
            outputs: [{ id: "out", label: "RGB", type: "rgba16f" }],
            params: {},
            typeName: "com.cpipe.tone.filmic_rgb",
            visualKind: "standard"
          }}
          selected={false}
        />
      </ReactFlowProvider>
    );

    expect(markup).toContain("Filmic RGB");
    expect(markup).toContain('title="tone"');
    expect(markup).toContain("background:#7c3aed");
    expect(markup).toContain("Thumbnail");
    expect(markup).toContain("Parameters");
  });
});
