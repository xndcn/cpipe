export const editorFrameTypes = {
  thumbnail: 0x01,
  profile: 0x02,
  log: 0x03,
  ack: 0x04,
  control: 0x10
} as const;

export type EditorFrameType = (typeof editorFrameTypes)[keyof typeof editorFrameTypes];

export interface EditorFrame {
  nodeId: number;
  payload: Uint8Array;
  timestampMs: number;
  type: EditorFrameType;
}

const headerBytes = 13;
const textEncoder = new TextEncoder();

function writeBe32(view: DataView, offset: number, value: number) {
  view.setUint32(offset, value >>> 0, false);
}

function readBe32(view: DataView, offset: number) {
  return view.getUint32(offset, false);
}

export function encodeEditorFrame(
  type: EditorFrameType,
  payload: Uint8Array | string,
  nodeId = 0,
  timestampMs = 0
) {
  const payloadBytes = typeof payload === "string" ? textEncoder.encode(payload) : payload;
  const frame = new Uint8Array(headerBytes + payloadBytes.byteLength);
  const view = new DataView(frame.buffer);
  frame[0] = type;
  writeBe32(view, 1, nodeId);
  writeBe32(view, 5, timestampMs);
  writeBe32(view, 9, payloadBytes.byteLength);
  frame.set(payloadBytes, headerBytes);
  return frame;
}

export function encodeControlFrame(
  payload: Record<string, unknown>,
  type: EditorFrameType = editorFrameTypes.control
) {
  return encodeEditorFrame(type, JSON.stringify(payload));
}

export function decodeEditorFrame(data: ArrayBuffer | ArrayBufferView) {
  const bytes =
    data instanceof ArrayBuffer
      ? new Uint8Array(data)
      : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (bytes.byteLength < headerBytes) {
    throw new Error("editor frame shorter than 13-byte header");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const length = readBe32(view, 9);
  if (bytes.byteLength !== headerBytes + length) {
    throw new Error("editor frame payload length mismatch");
  }
  return {
    nodeId: readBe32(view, 1),
    payload: bytes.slice(headerBytes),
    timestampMs: readBe32(view, 5),
    type: bytes[0] as EditorFrameType
  } satisfies EditorFrame;
}
