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

  it("renders every P3-PD-37 live-param control shape", () => {
    const p3Params: ManifestParam[] = [
      { name: "ev", type: "number", range: { min: -2, max: 2 }, default: 0 },
      { name: "contrast", type: "number", range: { min: 0.5, max: 2 }, default: 1 },
      { name: "saturation", type: "number", range: { min: 0, max: 2 }, default: 1 },
      { name: "highlights", type: "number", range: { min: 0, max: 2 }, default: 1 },
      { name: "shadows", type: "number", range: { min: 0, max: 2 }, default: 1 },
      { name: "white_point", type: "number", range: { min: 0.1, max: 10 }, default: 10 },
      { name: "toggle", type: "boolean", default: true },
      { name: "sigma", type: "number", range: { min: 0, max: 0.2 } },
      { name: "sigma_override", type: "number", range: { min: 0 }, default: 0 },
      { name: "radius", type: "number", range: { min: 1, max: 32 }, default: 1 },
      { name: "eps", type: "number", range: { min: 1e-5, max: 1e-1 }, default: 0.015 },
      { name: "chroma_strength", type: "number", range: { min: 0, max: 2 }, default: 1 },
      { name: "strength", type: "number", range: { min: 0, max: 2 }, default: 0.75 },
      { name: "threshold", type: "number", range: { min: 0, max: 0.1 }, default: 0 },
      { name: "lut_path", type: "string" },
      {
        name: "interpolation",
        type: "enum",
        enum_values: ["tetrahedral", "trilinear"],
        default: "tetrahedral"
      },
      { name: "weight_contrast", type: "number", range: { min: 0, max: 2 }, default: 0 },
      { name: "weight_saturation", type: "number", range: { min: 0, max: 2 }, default: 1 },
      {
        name: "weight_well_exposedness",
        type: "number",
        range: { min: 0, max: 2 },
        default: 1
      }
    ];

    const markup = renderToStaticMarkup(
      <ParameterForm nodeId="p3-pd-37" params={p3Params} values={{}} onCommit={vi.fn()} />
    );

    expect(markup.match(/type="range"/g)?.length).toBe(16);
    expect(markup).toContain('name="toggle"');
    expect(markup).toContain('type="checkbox"');
    expect(markup).toContain('name="lut_path"');
    expect(markup).toContain("<select");
    expect(markup).toContain("trilinear");
    expect(validateManifestParamValue(p3Params[6], false).valid).toBe(true);
    expect(validateManifestParamValue(p3Params[0], -3).valid).toBe(false);
  });
});
