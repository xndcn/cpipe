import { beforeEach, describe, expect, it, vi } from "vitest";

import { openPipelineJson, savePipelineJson } from "../src/ipc/files";

const pipeline = {
  version: "0.4",
  nodes: [{ id: "tone", type: "com.cpipe.tone.filmic_rgb", params: { ev: 0.5 } }],
  edges: []
};

describe("pipeline file access", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it("opens and saves through the File System Access API when available", async () => {
    const writable = { close: vi.fn(), write: vi.fn() };
    const handle = {
      createWritable: vi.fn(async () => writable),
      getFile: vi.fn(async () => new File([JSON.stringify(pipeline)], "pipeline.cpipe.json"))
    };
    const access = {
      showOpenFilePicker: vi.fn(async () => [handle]),
      showSaveFilePicker: vi.fn(async () => handle)
    };

    const opened = await openPipelineJson(access);
    const saved = await savePipelineJson(pipeline, { access, handle: opened.handle });

    expect(opened.source).toBe("fsa");
    expect(opened.pipeline).toEqual(pipeline);
    expect(writable.write).toHaveBeenCalledWith(JSON.stringify(pipeline, null, 2));
    expect(saved.source).toBe("fsa");
  });

  it("uses injected fallback open and Blob download when FSA is unavailable", async () => {
    const downloadBlob = vi.fn();
    const access = {
      downloadBlob,
      pickFallbackFile: vi.fn(
        async () => new File([JSON.stringify(pipeline)], "pipeline.cpipe.json")
      )
    };

    const opened = await openPipelineJson(access);
    const saved = await savePipelineJson(opened.pipeline, { access });

    expect(opened.source).toBe("fallback");
    expect(saved.source).toBe("fallback");
    expect(downloadBlob).toHaveBeenCalledOnce();
  });
});
