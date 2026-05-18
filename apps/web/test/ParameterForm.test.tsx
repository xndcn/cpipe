import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

import {
  ParameterForm,
  validateManifestParamValue,
  type ManifestParam
} from "../src/components/nodes/ParameterForm";

const evParam: ManifestParam = {
  name: "ev",
  type: "number",
  range: { min: -2, max: 2 },
  default: 0
};

describe("ParameterForm", () => {
  it("renders numeric params as sliders and enum params as selects", () => {
    const markup = renderToStaticMarkup(
      <ParameterForm
        nodeId="tone"
        params={[
          evParam,
          {
            name: "mode",
            type: "enum",
            enum_values: ["tetrahedral", "trilinear"],
            default: "tetrahedral"
          }
        ]}
        values={{ ev: 0, mode: "tetrahedral" }}
        onCommit={vi.fn()}
      />
    );

    expect(markup).toContain('type="range"');
    expect(markup).toContain('name="ev"');
    expect(markup).toContain("<select");
    expect(markup).toContain("tetrahedral");
  });

  it("rejects out-of-range values before sending updates", () => {
    expect(validateManifestParamValue(evParam, 1.5).valid).toBe(true);
    const result = validateManifestParamValue(evParam, 2.5);

    expect(result.valid).toBe(false);
    expect(result.message).toContain("must be <= 2");
  });
});
