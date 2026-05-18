import { beforeEach, describe, expect, it } from "vitest";

import { createRuntimeClient } from "../src/ipc/rpc";
import { decodeEditorFrame, encodeControlFrame, editorFrameTypes } from "../src/ipc/frames";
import { useDeviceStore } from "../src/store/device";
import { usePipelineStore } from "../src/store/pipeline";

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  binaryType: BinaryType = "arraybuffer";
  onmessage: ((event: MessageEvent<ArrayBuffer>) => void) | null = null;
  onopen: (() => void) | null = null;
  sent: Uint8Array[] = [];

  constructor(readonly url: string) {
    MockWebSocket.instances.push(this);
  }

  close() {}

  emit(bytes: Uint8Array) {
    this.onmessage?.({ data: bytes.buffer } as MessageEvent<ArrayBuffer>);
  }

  open() {
    this.onopen?.();
  }

  send(data: ArrayBuffer | ArrayBufferView | Blob | string) {
    if (data instanceof Uint8Array) {
      this.sent.push(data);
      return;
    }
    if (ArrayBuffer.isView(data)) {
      this.sent.push(new Uint8Array(data.buffer));
      return;
    }
    if (data instanceof ArrayBuffer) {
      this.sent.push(new Uint8Array(data));
    }
  }
}

describe("runtime client", () => {
  beforeEach(() => {
    MockWebSocket.instances = [];
    useDeviceStore.setState(useDeviceStore.getInitialState());
    usePipelineStore.setState(usePipelineStore.getInitialState());
  });

  it("sends update-param controls and records ack frames", () => {
    const client = createRuntimeClient({
      url: "ws://127.0.0.1:4747/ws",
      websocketFactory: (url) => new MockWebSocket(url) as unknown as WebSocket,
      onAck: (payload) => useDeviceStore.getState().recordAck(payload)
    });

    client.connect();
    MockWebSocket.instances[0].open();
    client.updateParam("tone", "ev", 0.5);

    const sent = decodeEditorFrame(MockWebSocket.instances[0].sent[0]);
    expect(sent.type).toBe(editorFrameTypes.control);
    expect(JSON.parse(new TextDecoder().decode(sent.payload))).toEqual({
      type: "node.update_param",
      node_id: "tone",
      key: "ev",
      value: 0.5
    });

    MockWebSocket.instances[0].emit(
      encodeControlFrame({ ok: true, received: "node.update_param" }, editorFrameTypes.ack)
    );
    expect(useDeviceStore.getState().lastAck).toEqual({
      ok: true,
      received: "node.update_param"
    });
  });

  it("stores WebP thumbnail frames and applies post-resolution pipelines", () => {
    usePipelineStore.getState().loadPipeline({ version: "0.4", nodes: [], edges: [] });
    const client = createRuntimeClient({
      url: "ws://127.0.0.1:4747/ws",
      websocketFactory: (url) => new MockWebSocket(url) as unknown as WebSocket,
      onAppliedPipeline: (pipeline) => usePipelineStore.getState().loadPipeline(pipeline),
      onThumbnail: (nodeId, bytes) => useDeviceStore.getState().recordThumbnail(nodeId, bytes)
    });

    client.connect();
    MockWebSocket.instances[0].open();
    MockWebSocket.instances[0].emit(
      encodeControlFrame(
        {
          type: "pipeline.applied",
          pipeline: {
            version: "0.4",
            nodes: [{ id: "convert", type: "com.cpipe.precision_convert", params: {} }],
            edges: []
          }
        },
        editorFrameTypes.control
      )
    );
    MockWebSocket.instances[0].emit(
      new Uint8Array([
        editorFrameTypes.thumbnail,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        82,
        73,
        70,
        70
      ])
    );

    expect(usePipelineStore.getState().nodes[0].data.visualKind).toBe("convert");
    expect(useDeviceStore.getState().thumbnailUrls.__latest).toBeTruthy();
  });
});
