import { useState } from "react";

import { openPipelineJson, savePipelineJson, type PipelineFileHandle } from "../../ipc/files";
import { loadRecentGraphs, rememberRecentGraph, type RecentGraph } from "../../store/persist";
import { usePipelineStore } from "../../store/pipeline";

export function Library({ recentGraphs }: { recentGraphs?: RecentGraph[] }) {
  const loadPipeline = usePipelineStore((state) => state.loadPipeline);
  const pipeline = usePipelineStore((state) => state.pipeline);
  const [fileHandle, setFileHandle] = useState<PipelineFileHandle | undefined>();
  const [recent, setRecent] = useState<RecentGraph[]>(recentGraphs ?? loadRecentGraphs());

  async function openFile() {
    const opened = await openPipelineJson();
    loadPipeline(opened.pipeline);
    setFileHandle(opened.handle);
    const entry = rememberRecentGraph({
      id: opened.pipeline.id ?? "pipeline.cpipe.json",
      pipeline: opened.pipeline,
      savedAt: Date.now()
    });
    setRecent(entry);
  }

  async function saveFile() {
    if (pipeline === null) {
      return;
    }
    const saved = await savePipelineJson(pipeline, { handle: fileHandle });
    setFileHandle(saved.handle ?? fileHandle);
    const entry = rememberRecentGraph({
      id: pipeline.id ?? "pipeline.cpipe.json",
      pipeline,
      savedAt: Date.now()
    });
    setRecent(entry);
  }

  return (
    <aside className="library-panel">
      <div className="library-panel__actions">
        <button onClick={() => void openFile()} type="button">
          Open
        </button>
        <button disabled={pipeline === null} onClick={() => void saveFile()} type="button">
          Save
        </button>
      </div>
      <div className="library-panel__recent">
        {recent.map((entry) => (
          <button
            key={`${entry.id}:${entry.savedAt}`}
            onClick={() => loadPipeline(entry.pipeline)}
            type="button"
          >
            {entry.id}
          </button>
        ))}
      </div>
    </aside>
  );
}
