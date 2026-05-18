import { Handle, Position } from "@xyflow/react";

import type { CpipeNodeData, CpipePort } from "../../store/pipeline";

function PortList({ ports, side }: { ports: CpipePort[]; side: "input" | "output" }) {
  const position = side === "input" ? Position.Left : Position.Right;

  return (
    <div className={`base-node__ports base-node__ports--${side}`}>
      {ports.map((port) => (
        <div className="base-node__port" key={port.id} title={`${port.label}: ${port.type}`}>
          <Handle id={port.id} position={position} type={side === "input" ? "target" : "source"} />
          <span>{port.label}</span>
        </div>
      ))}
    </div>
  );
}

export function BaseNode({ data, selected = false }: { data: CpipeNodeData; selected?: boolean }) {
  const isConvert = data.visualKind === "convert";
  return (
    <article
      className={`base-node${selected ? " base-node--selected" : ""}${
        isConvert ? " base-node--convert" : ""
      }`}
    >
      <header className="base-node__header" title={data.nodeId}>
        <span className="base-node__swatch" style={{ background: data.categoryColor }} />
        <span className="base-node__label">{data.label}</span>
        <span className="base-node__id">{data.nodeId}</span>
      </header>
      <div className="base-node__body">
        <div className="base-node__thumbnail">Thumbnail</div>
        {isConvert ? null : (
          <div className="base-node__params">
            {Object.keys(data.params).length === 0
              ? "Parameters"
              : `${Object.keys(data.params).length} params`}
          </div>
        )}
      </div>
      <footer className="base-node__footer">
        <PortList ports={data.inputs} side="input" />
        <span className="base-node__type">{data.category}</span>
        <PortList ports={data.outputs} side="output" />
      </footer>
    </article>
  );
}
