import { create } from "zustand";

export type PipelineStatus = "idle" | "loading" | "ready";

export interface PipelineState {
  status: PipelineStatus;
  setStatus: (status: PipelineStatus) => void;
}

export const usePipelineStore = create<PipelineState>((set) => ({
  status: "idle",
  setStatus: (status) => set({ status })
}));
