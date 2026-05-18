import type { CpipePipeline } from "../store/pipeline";
import { decodeEditorFrame, encodeControlFrame, editorFrameTypes } from "./frames";

export interface RuntimeClientOptions {
  onAck?: (payload: Record<string, unknown>) => void;
  onAppliedPipeline?: (pipeline: CpipePipeline) => void;
  onLog?: (payload: Record<string, unknown>) => void;
  onProfile?: (payload: Record<string, unknown>) => void;
  onThumbnail?: (nodeId: string, bytes: Uint8Array) => void;
  url: string;
  websocketFactory?: (url: string) => WebSocket;
}

const textDecoder = new TextDecoder();

function parseJsonPayload(payload: Uint8Array) {
  return JSON.parse(textDecoder.decode(payload)) as Record<string, unknown>;
}

function defaultWebSocketFactory(url: string) {
  return new WebSocket(url);
}

export function createRuntimeClient(options: RuntimeClientOptions) {
  let socket: WebSocket | undefined;
  const websocketFactory = options.websocketFactory ?? defaultWebSocketFactory;

  function sendControl(payload: Record<string, unknown>) {
    socket?.send(encodeControlFrame(payload));
  }

  return {
    close() {
      socket?.close();
      socket = undefined;
    },
    connect() {
      socket = websocketFactory(options.url);
      socket.binaryType = "arraybuffer";
      socket.onmessage = (event: MessageEvent<ArrayBuffer>) => {
        const frame = decodeEditorFrame(event.data);
        if (frame.type === editorFrameTypes.thumbnail) {
          options.onThumbnail?.("__latest", frame.payload);
          return;
        }
        const payload = parseJsonPayload(frame.payload);
        if (frame.type === editorFrameTypes.ack) {
          options.onAck?.(payload);
          return;
        }
        if (frame.type === editorFrameTypes.profile) {
          options.onProfile?.(payload);
          return;
        }
        if (frame.type === editorFrameTypes.log) {
          options.onLog?.(payload);
          return;
        }
        if (payload.type === "pipeline.applied" && typeof payload.pipeline === "object") {
          options.onAppliedPipeline?.(payload.pipeline as CpipePipeline);
        }
      };
    },
    subscribeThumbnail(nodeId: string, port = "rgb", maxSize = 256, fps = 5) {
      sendControl({
        type: "node.subscribe_thumbnail",
        node_id: nodeId,
        port,
        max_size: maxSize,
        fps
      });
    },
    unsubscribeThumbnail(nodeId: string, port = "rgb") {
      sendControl({ type: "node.unsubscribe_thumbnail", node_id: nodeId, port });
    },
    updateParam(nodeId: string, key: string, value: unknown) {
      sendControl({ type: "node.update_param", node_id: nodeId, key, value });
    }
  };
}
