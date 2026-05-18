import { useEffect, useMemo } from "react";

import { useDeviceStore } from "../../store/device";
import { usePipelineStore } from "../../store/pipeline";
import { ParameterForm, type ManifestParam } from "../nodes/ParameterForm";

export function NodeInspector() {
  const nodes = usePipelineStore((state) => state.nodes);
  const selectedId = usePipelineStore((state) => state.selectedId);
  const manifestsByType = useDeviceStore((state) => state.manifestsByType);
  const sendParamUpdate = useDeviceStore((state) => state.sendParamUpdate);
  const subscribeSelectedThumbnail = useDeviceStore((state) => state.subscribeSelectedThumbnail);
  const thumbnailUrls = useDeviceStore((state) => state.thumbnailUrls);

  const selectedNode = useMemo(
    () => nodes.find((node) => node.id === selectedId),
    [nodes, selectedId]
  );
  const params = (
    selectedNode === undefined ? [] : (manifestsByType[selectedNode.data.typeName]?.params ?? [])
  ) as ManifestParam[];
  const values = (selectedNode?.data.params ?? {}) as Record<string, unknown>;
  const thumbnailUrl =
    selectedNode === undefined
      ? undefined
      : (thumbnailUrls[selectedNode.id] ?? thumbnailUrls.__latest);

  useEffect(() => {
    if (selectedNode !== undefined) {
      subscribeSelectedThumbnail(selectedNode.id);
    }
  }, [selectedNode, subscribeSelectedThumbnail]);

  if (selectedNode === undefined) {
    return (
      <aside className="node-inspector">
        <p className="node-inspector__empty">Select a node</p>
      </aside>
    );
  }

  return (
    <aside className="node-inspector">
      <header className="node-inspector__header">
        <span>{selectedNode.data.label}</span>
        <span>{selectedNode.id}</span>
      </header>
      <div className="node-inspector__thumbnail">
        {thumbnailUrl === undefined ? (
          <span>No thumbnail</span>
        ) : (
          <img alt={`${selectedNode.id} thumbnail`} src={thumbnailUrl} />
        )}
      </div>
      <ParameterForm
        nodeId={selectedNode.id}
        onCommit={(key, value) => sendParamUpdate(selectedNode.id, key, value)}
        params={params}
        values={values}
      />
    </aside>
  );
}
