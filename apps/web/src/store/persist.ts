import type { CpipePipeline } from "./pipeline";

const storageKey = "cpipe.editor";
export const RECENT_GRAPHS_KEY = "cpipe.recentGraphs";
const maxRecentGraphs = 10;

export interface RecentGraph {
  id: string;
  pipeline: CpipePipeline;
  savedAt: number;
}

export function editorStorageKey() {
  return storageKey;
}

export function loadRecentGraphs(): RecentGraph[] {
  if (typeof localStorage === "undefined") {
    return [];
  }
  const raw = localStorage.getItem(RECENT_GRAPHS_KEY);
  if (raw === null) {
    return [];
  }
  try {
    const parsed = JSON.parse(raw) as RecentGraph[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function rememberRecentGraph(entry: RecentGraph) {
  const next = [
    entry,
    ...loadRecentGraphs().filter((candidate) => candidate.id !== entry.id)
  ].slice(0, maxRecentGraphs);
  if (typeof localStorage !== "undefined") {
    localStorage.setItem(RECENT_GRAPHS_KEY, JSON.stringify(next));
  }
  return next;
}
