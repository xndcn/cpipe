import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, describe, expect, it } from "vitest";

import { App } from "../src/App";
import { editorBanner } from "../src/components/EditorShell";
import { useDeviceStore } from "../src/store/device";
import { useSchemaStore } from "../src/store/schema";

describe("App", () => {
  beforeEach(() => {
    useDeviceStore.setState(useDeviceStore.getInitialState());
    useSchemaStore.setState(useSchemaStore.getInitialState());
  });

  it("renders the scaffold loading state", () => {
    expect(renderToStaticMarkup(<App />)).toContain("cpipe editor loading");
  });

  it("shows the offline runtime banner when schema sync has no banner", () => {
    expect(editorBanner(undefined, "no runtime connected")).toBe("no runtime connected");
  });
});
