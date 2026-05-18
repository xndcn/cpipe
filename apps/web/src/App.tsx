import { useEffect } from "react";

import { EditorShell } from "./components/EditorShell";
import type { CpipePipeline } from "./store/pipeline";
import { usePipelineStore } from "./store/pipeline";
import { useSchemaStore } from "./store/schema";

const examplePipelineUrl = "examples/pipelines/full-classic-pipeline.cpipe.json";
const defaultRuntimeUrl = "http://127.0.0.1:4747";

export function App() {
  const loadPipeline = usePipelineStore((state) => state.loadPipeline);
  const setStatus = usePipelineStore((state) => state.setStatus);
  const connectSchemas = useSchemaStore((state) => state.connect);

  useEffect(() => {
    let cancelled = false;

    async function loadInitialPipeline() {
      setStatus("loading");
      const response = await fetch(examplePipelineUrl);
      if (!response.ok) {
        throw new Error(`failed to load ${examplePipelineUrl}`);
      }
      const pipeline = (await response.json()) as CpipePipeline;
      if (!cancelled) {
        loadPipeline(pipeline);
      }
    }

    void loadInitialPipeline().catch(() => {
      if (!cancelled) {
        setStatus("idle");
      }
    });
    void connectSchemas(defaultRuntimeUrl);

    return () => {
      cancelled = true;
    };
  }, [connectSchemas, loadPipeline, setStatus]);

  return <EditorShell />;
}
