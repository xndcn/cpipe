import { FlowCanvas } from "./FlowCanvas";
import { usePipelineStore } from "../store/pipeline";
import { useSchemaStore } from "../store/schema";

export function EditorShell() {
  const nodeCount = usePipelineStore((state) => state.nodes.length);
  const status = usePipelineStore((state) => state.status);
  const schemaBanner = useSchemaStore((state) => state.banner);

  return (
    <main className="editor-shell" data-status={status}>
      <header className="editor-shell__topbar">
        <span className="editor-shell__brand">cpipe editor</span>
        <span className="editor-shell__status">
          {status === "ready" ? `${nodeCount} nodes` : "cpipe editor loading…"}
        </span>
      </header>
      {schemaBanner === undefined ? null : (
        <div className="editor-shell__banner" role="status">
          {schemaBanner}
        </div>
      )}
      <section className="editor-shell__canvas" aria-label="Pipeline graph">
        <FlowCanvas />
      </section>
    </main>
  );
}
