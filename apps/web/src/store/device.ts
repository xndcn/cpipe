import { create } from "zustand";

import { createRuntimeClient } from "../ipc/rpc";
import type { CpipePipeline } from "./pipeline";
import { usePipelineStore } from "./pipeline";

export type RuntimeStatus = "idle" | "connecting" | "connected" | "offline";

export interface NodeManifest {
  id: string;
  params?: unknown[];
}

interface RuntimeClientHandle {
  close: () => void;
  connect: () => void;
  subscribeThumbnail: (nodeId: string, port?: string, maxSize?: number, fps?: number) => void;
  unsubscribeThumbnail: (nodeId: string, port?: string) => void;
  updateParam: (nodeId: string, key: string, value: unknown) => void;
}

export interface DeviceState {
  banner?: string;
  client?: RuntimeClientHandle;
  connectRuntime: (runtimeBaseUrl: string) => Promise<void>;
  lastAck?: Record<string, unknown>;
  lastLog?: Record<string, unknown>;
  lastProfile?: Record<string, unknown>;
  manifestsByType: Record<string, NodeManifest>;
  recordAck: (payload: Record<string, unknown>) => void;
  recordLog: (payload: Record<string, unknown>) => void;
  recordProfile: (payload: Record<string, unknown>) => void;
  recordThumbnail: (nodeId: string, bytes: Uint8Array) => void;
  sendParamUpdate: (nodeId: string, key: string, value: unknown) => void;
  status: RuntimeStatus;
  subscribeSelectedThumbnail: (nodeId: string, port?: string) => void;
  thumbnailUrls: Record<string, string>;
}

function websocketUrl(runtimeBaseUrl: string) {
  const url = new URL("/ws", runtimeBaseUrl);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url.toString();
}

function thumbnailUrl(nodeId: string, bytes: Uint8Array) {
  if (typeof URL !== "undefined" && typeof URL.createObjectURL === "function") {
    return URL.createObjectURL(new Blob([bytes], { type: "image/webp" }));
  }
  return `webp:${nodeId}:${bytes.byteLength}`;
}

async function fetchRegistry(runtimeBaseUrl: string) {
  const response = await fetch(new URL("/api/registry/nodes", runtimeBaseUrl));
  if (!response.ok) {
    throw new Error(`registry fetch failed: ${response.status}`);
  }
  const envelope = (await response.json()) as {
    data?: { nodes?: Array<{ id: string; manifest: NodeManifest }> };
  };
  const manifests: Record<string, NodeManifest> = {};
  for (const entry of envelope.data?.nodes ?? []) {
    manifests[entry.id] = entry.manifest;
  }
  return manifests;
}

export const useDeviceStore = create<DeviceState>((set, get) => ({
  banner: undefined,
  connectRuntime: async (runtimeBaseUrl) => {
    set({ status: "connecting" });
    try {
      const manifestsByType = await fetchRegistry(runtimeBaseUrl);
      const client = createRuntimeClient({
        url: websocketUrl(runtimeBaseUrl),
        onAck: (payload) => get().recordAck(payload),
        onAppliedPipeline: (pipeline: CpipePipeline) =>
          usePipelineStore.getState().loadPipeline(pipeline),
        onLog: (payload) => get().recordLog(payload),
        onProfile: (payload) => get().recordProfile(payload),
        onThumbnail: (nodeId, bytes) => get().recordThumbnail(nodeId, bytes)
      });
      client.connect();
      set({ banner: undefined, client, manifestsByType, status: "connected" });
    } catch {
      set({
        banner: "no runtime connected",
        status: "offline"
      });
    }
  },
  lastAck: undefined,
  lastLog: undefined,
  lastProfile: undefined,
  manifestsByType: {},
  recordAck: (payload) => set({ lastAck: payload }),
  recordLog: (payload) => set({ lastLog: payload }),
  recordProfile: (payload) => set({ lastProfile: payload }),
  recordThumbnail: (nodeId, bytes) =>
    set((state) => ({
      thumbnailUrls: {
        ...state.thumbnailUrls,
        [nodeId]: thumbnailUrl(nodeId, bytes)
      }
    })),
  sendParamUpdate: (nodeId, key, value) => {
    usePipelineStore.getState().updateNodeParam(nodeId, key, value);
    get().client?.updateParam(nodeId, key, value);
  },
  status: "idle",
  subscribeSelectedThumbnail: (nodeId, port = "rgb") => {
    get().client?.subscribeThumbnail(nodeId, port, 256, 5);
  },
  thumbnailUrls: {}
}));
