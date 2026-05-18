import { FlowCanvas } from "./FlowCanvas";
import { DevicePane } from "./panels/DevicePane";
import { NodeInspector } from "./panels/NodeInspector";
import { useDeviceStore } from "../store/device";
import { usePipelineStore } from "../store/pipeline";
import { useSchemaStore } from "../store/schema";

export function EditorShell() {
  const nodeCount = usePipelineStore((state) => state.nodes.length);
  const status = usePipelineStore((state) => state.status);
  const deviceBanner = useDeviceStore((state) => state.banner);
  const schemaBanner = useSchemaStore((state) => state.banner);
  const banner = schemaBanner ?? deviceBanner;

  return (
    <main className="editor-shell" data-status={status}>
      <header className="editor-shell__topbar">
        <span className="editor-shell__brand">cpipe editor</span>
        <span className="editor-shell__status">
          {status === "ready" ? `${nodeCount} nodes` : "cpipe editor loading…"}
        </span>
      </header>
      {banner === undefined ? null : (
        <div className="editor-shell__banner" role="status">
          {banner}
        </div>
      )}
      <section className="editor-shell__workspace">
        <div className="editor-shell__canvas" aria-label="Pipeline graph">
          <FlowCanvas />
        </div>
        <NodeInspector />
      </section>
      <DevicePane />
    </main>
  );
}
