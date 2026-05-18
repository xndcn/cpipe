import Ajv, { type AnySchema } from "ajv";
import { create } from "zustand";

export const schemaValidator = new Ajv({
  allErrors: true,
  strict: false
});

export const SCHEMA_CACHE_KEY = "cpipe.schemas";
export const schemaCacheTtlMs = 7 * 24 * 60 * 60 * 1000;

export interface CachedSchemaEntry {
  fetchedAt: number;
  expiresAt: number;
  nodeSchema: unknown;
  pipelineSchema: unknown;
}

export interface SchemaCache {
  entries: Record<string, CachedSchemaEntry>;
}

export type SchemaStatus = "idle" | "ready" | "stale" | "error";

export interface SchemaSyncResult {
  banner?: string;
  entry?: CachedSchemaEntry;
  status: SchemaStatus;
}

export interface SchemaState extends SchemaSyncResult {
  connect: (runtimeBaseUrl: string) => Promise<void>;
  validators: {
    node?: ReturnType<Ajv["compile"]>;
    pipeline?: ReturnType<Ajv["compile"]>;
  };
}

export function schemaCacheKey(runtimeBaseUrl: string) {
  const url = new URL(runtimeBaseUrl);
  return `${url.protocol}//${url.host}`;
}

export function loadCachedSchemas(): SchemaCache {
  if (typeof localStorage === "undefined") {
    return { entries: {} };
  }
  const raw = localStorage.getItem(SCHEMA_CACHE_KEY);
  if (raw === null) {
    return { entries: {} };
  }
  try {
    return JSON.parse(raw) as SchemaCache;
  } catch {
    return { entries: {} };
  }
}

function storeCachedSchemas(cache: SchemaCache) {
  if (typeof localStorage !== "undefined") {
    localStorage.setItem(SCHEMA_CACHE_KEY, JSON.stringify(cache));
  }
}

async function fetchJson(url: string) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`schema fetch failed: ${response.status}`);
  }
  return response.json() as Promise<unknown>;
}

export async function synchronizeSchemas(
  runtimeBaseUrl: string,
  now = Date.now()
): Promise<SchemaSyncResult> {
  const key = schemaCacheKey(runtimeBaseUrl);
  const cache = loadCachedSchemas();
  const cached = cache.entries[key];

  if (cached !== undefined && cached.expiresAt > now) {
    return { entry: cached, status: "ready" };
  }

  try {
    const [nodeSchema, pipelineSchema] = await Promise.all([
      fetchJson(new URL("/api/schemas/node", runtimeBaseUrl).toString()),
      fetchJson(new URL("/api/schemas/pipeline", runtimeBaseUrl).toString())
    ]);
    const entry = {
      fetchedAt: now,
      expiresAt: now + schemaCacheTtlMs,
      nodeSchema,
      pipelineSchema
    };
    storeCachedSchemas({ entries: { ...cache.entries, [key]: entry } });
    return { entry, status: "ready" };
  } catch {
    if (cached !== undefined) {
      return {
        banner:
          "Runtime unreachable; cached schemas are stale. Editing is limited until reconnect.",
        entry: cached,
        status: "stale"
      };
    }
    return {
      status: "error"
    };
  }
}

export const useSchemaStore = create<SchemaState>((set) => ({
  banner: undefined,
  connect: async (runtimeBaseUrl) => {
    const result = await synchronizeSchemas(runtimeBaseUrl);
    const validators: SchemaState["validators"] = {};
    if (result.entry !== undefined) {
      validators.node = schemaValidator.compile(result.entry.nodeSchema as AnySchema);
      validators.pipeline = schemaValidator.compile(result.entry.pipelineSchema as AnySchema);
    }
    set({ ...result, validators });
  },
  status: "idle",
  validators: {}
}));
