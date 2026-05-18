import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  SCHEMA_CACHE_KEY,
  loadCachedSchemas,
  schemaCacheKey,
  schemaCacheTtlMs,
  synchronizeSchemas
} from "../src/store/schema";

const nodeSchema = { $id: "node", type: "object" };
const pipelineSchema = { $id: "pipeline", type: "object" };

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

describe("schema cache", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    Object.defineProperty(globalThis, "localStorage", {
      configurable: true,
      value: new MemoryStorage()
    });
  });

  it("fetches node and pipeline schemas and stores a seven-day cache entry", async () => {
    vi.spyOn(globalThis, "fetch").mockImplementation(async (input) => {
      const url = String(input);
      return new Response(JSON.stringify(url.endsWith("/node") ? nodeSchema : pipelineSchema), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      });
    });

    const result = await synchronizeSchemas("http://127.0.0.1:4747", 1000);
    const cache = loadCachedSchemas();
    const entry = cache.entries[schemaCacheKey("http://127.0.0.1:4747")];

    expect(result.status).toBe("ready");
    expect(entry.expiresAt - entry.fetchedAt).toBe(schemaCacheTtlMs);
    expect(localStorage.getItem(SCHEMA_CACHE_KEY)).toContain("127.0.0.1:4747");
  });

  it("reports a stale-cache banner when the runtime is unreachable", async () => {
    localStorage.setItem(
      SCHEMA_CACHE_KEY,
      JSON.stringify({
        entries: {
          [schemaCacheKey("http://127.0.0.1:4747")]: {
            fetchedAt: 0,
            expiresAt: 1,
            nodeSchema,
            pipelineSchema
          }
        }
      })
    );
    vi.spyOn(globalThis, "fetch").mockRejectedValue(new Error("offline"));

    const result = await synchronizeSchemas("http://127.0.0.1:4747", 1000);

    expect(result.status).toBe("stale");
    expect(result.banner).toContain("cached schemas are stale");
  });
});
