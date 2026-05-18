import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { Library } from "../src/components/panels/Library";

describe("Library", () => {
  it("renders offline open/save controls and recent graph entries", () => {
    const markup = renderToStaticMarkup(
      <Library
        recentGraphs={[
          {
            id: "pipeline.cpipe.json",
            pipeline: { version: "0.4", nodes: [], edges: [] },
            savedAt: 1
          }
        ]}
      />
    );

    expect(markup).toContain("Open");
    expect(markup).toContain("Save");
    expect(markup).toContain("pipeline.cpipe.json");
  });
});
