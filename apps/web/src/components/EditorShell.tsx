import { usePipelineStore } from "../store/pipeline";

export function EditorShell() {
  const status = usePipelineStore((state) => state.status);

  return (
    <main className="editor-shell" data-status={status}>
      <div className="editor-shell__status">cpipe editor loading…</div>
    </main>
  );
}
