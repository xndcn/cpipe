import type { CpipePipeline } from "../store/pipeline";

export type FileAccessSource = "fallback" | "fsa";

interface WritableFile {
  close: () => Promise<void> | void;
  write: (data: string) => Promise<void> | void;
}

export interface PipelineFileHandle {
  createWritable?: () => Promise<WritableFile>;
  getFile: () => Promise<File>;
}

export interface PipelineFileAccess {
  downloadBlob?: (blob: Blob, filename: string) => void;
  pickFallbackFile?: () => Promise<File>;
  showOpenFilePicker?: (options?: unknown) => Promise<PipelineFileHandle[]>;
  showSaveFilePicker?: (options?: unknown) => Promise<PipelineFileHandle>;
}

export interface OpenPipelineResult {
  handle?: PipelineFileHandle;
  pipeline: CpipePipeline;
  source: FileAccessSource;
}

export interface SavePipelineOptions {
  access?: PipelineFileAccess;
  handle?: PipelineFileHandle;
}

export interface SavePipelineResult {
  handle?: PipelineFileHandle;
  source: FileAccessSource;
}

const pickerOptions = {
  excludeAcceptAllOption: false,
  multiple: false,
  types: [
    {
      accept: { "application/json": [".json", ".cpipe.json"] },
      description: "cpipe pipeline JSON"
    }
  ]
};

function defaultAccess(): PipelineFileAccess {
  return globalThis as PipelineFileAccess;
}

function parsePipeline(text: string) {
  return JSON.parse(text) as CpipePipeline;
}

function stringifyPipeline(pipeline: CpipePipeline) {
  return JSON.stringify(pipeline, null, 2);
}

function pickFallbackFile() {
  return new Promise<File>((resolve, reject) => {
    const input = document.createElement("input");
    input.accept = ".json,.cpipe.json,application/json";
    input.type = "file";
    input.onchange = () => {
      const file = input.files?.[0];
      if (file === undefined) {
        reject(new Error("no pipeline file selected"));
        return;
      }
      resolve(file);
    };
    input.click();
  });
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.download = filename;
  link.href = url;
  link.click();
  URL.revokeObjectURL(url);
}

export async function openPipelineJson(access: PipelineFileAccess = defaultAccess()) {
  if (access.showOpenFilePicker !== undefined) {
    const [handle] = await access.showOpenFilePicker(pickerOptions);
    const file = await handle.getFile();
    return {
      handle,
      pipeline: parsePipeline(await file.text()),
      source: "fsa"
    } satisfies OpenPipelineResult;
  }

  const file = await (access.pickFallbackFile ?? pickFallbackFile)();
  return {
    pipeline: parsePipeline(await file.text()),
    source: "fallback"
  } satisfies OpenPipelineResult;
}

export async function savePipelineJson(
  pipeline: CpipePipeline,
  { access = defaultAccess(), handle }: SavePipelineOptions = {}
) {
  const text = stringifyPipeline(pipeline);
  const target =
    handle?.createWritable !== undefined
      ? handle
      : access.showSaveFilePicker === undefined
        ? undefined
        : await access.showSaveFilePicker(pickerOptions);

  if (target?.createWritable !== undefined) {
    const writable = await target.createWritable();
    await writable.write(text);
    await writable.close();
    return { handle: target, source: "fsa" } satisfies SavePipelineResult;
  }

  const blob = new Blob([text], { type: "application/json" });
  (access.downloadBlob ?? downloadBlob)(blob, "pipeline.cpipe.json");
  return { source: "fallback" } satisfies SavePipelineResult;
}
