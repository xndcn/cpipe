import { beforeEach, describe, expect, it } from "vitest";

import { RECENT_GRAPHS_KEY, loadRecentGraphs, rememberRecentGraph } from "../src/store/persist";

class MemoryStorage implements Storage {
  private readonly items = new Map<string, string>();
  length = 0;

  clear() {
    this.items.clear();
    this.length = 0;
  }

  getItem(key: string) {
    return this.items.get(key) ?? null;
  }

  key(index: number) {
    return Array.from(this.items.keys())[index] ?? null;
  }

  removeItem(key: string) {
    this.items.delete(key);
    this.length = this.items.size;
  }

  setItem(key: string, value: string) {
    this.items.set(key, value);
    this.length = this.items.size;
  }
}

describe("recent graph persistence", () => {
  beforeEach(() => {
    Object.defineProperty(globalThis, "localStorage", {
      configurable: true,
      value: new MemoryStorage()
    });
  });

  it("keeps the last ten graphs with most-recent first ordering", () => {
    for (let index = 0; index < 12; ++index) {
      rememberRecentGraph({
        id: `graph-${index}`,
        pipeline: { version: "0.4", nodes: [], edges: [] },
        savedAt: index
      });
    }

    const recent = loadRecentGraphs();

    expect(recent).toHaveLength(10);
    expect(recent[0].id).toBe("graph-11");
    expect(recent.at(-1)?.id).toBe("graph-2");
    expect(localStorage.getItem(RECENT_GRAPHS_KEY)).toContain("graph-11");
  });
});
