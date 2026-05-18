import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { App } from "../src/App";

describe("App", () => {
  it("renders the scaffold loading state", () => {
    expect(renderToStaticMarkup(<App />)).toContain("cpipe editor loading");
  });
});
