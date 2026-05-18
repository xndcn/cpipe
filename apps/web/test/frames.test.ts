import { describe, expect, it } from "vitest";

import { decodeEditorFrame, encodeControlFrame, editorFrameTypes } from "../src/ipc/frames";

describe("editor protocol frames", () => {
  it("encodes control frames with the P3 13-byte header", () => {
    const encoded = encodeControlFrame({
      type: "node.update_param",
      node_id: "tone",
      key: "ev",
      value: 0.5
    });
    const decoded = decodeEditorFrame(encoded);

    expect(encoded[0]).toBe(editorFrameTypes.control);
    expect(decoded).toMatchObject({
      type: editorFrameTypes.control,
      nodeId: 0,
      timestampMs: 0
    });
    expect(JSON.parse(new TextDecoder().decode(decoded.payload))).toEqual({
      type: "node.update_param",
      node_id: "tone",
      key: "ev",
      value: 0.5
    });
  });
});
