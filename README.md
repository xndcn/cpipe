# cpipe

> ⚠️ **Pre-alpha.** Phase 2 (`v0.3` — Classic Nodes + HDR) is released; Phase 3 (`v0.4` — Editor + Quality Harness) is in progress per [`docs/phase-03-editor-iqa.md`](docs/phase-03-editor-iqa.md). APIs remain unstable; the Linux DNG → SDR/HDR HEIF full-classic pipelines are usable for the pinned Pixel 8 Pro corpus path.

A computational photography pipeline. DAG, plugin nodes, zero-copy buffers, runs on CPU + GPU + NPU.

## What is cpipe?

cpipe (Computational Photography Pipeline) is a soft ISP for raw photographs. Pipelines are static DAGs of plugin nodes; they execute across CPU, GPU, and NPU with zero-copy buffers, and they are editable through a node-based Web Editor served from GitHub Pages.

**v1.0 delivers:**

- **Linux x86_64 CLI** — DNG in, SDR + HDR (PQ) HEIF out.
- **Web Editor on GitHub Pages** (React Flow) — pairs with a local runtime over plaintext HTTP / WebSocket on loopback / LAN; an offline JSON-only mode also opens and saves `pipeline.cpipe.json` without a runtime.
- **18 classic + 3 AI ISP nodes** — RCD / AMaZE / bilinear demosaic, dual-illuminant WB, filmic-RGB and Mertens tone, BM3D / guided-filter / wavelet denoise, edge-aware USM, DNG OpcodeList3 lens correction, 3D-LUT color; NAFNet-w32 denoise, AdaInt 3D-LUT, HDR+ Wronski burst neural denoise.
- **Plugin C ABI + JSON manifest** — first-party nodes today, third-party `.so` loading reserved for v2.
- **Color management** — linear Rec.2020 D65 working space; OCIO Looks menu; ICC profile embed + CICP signalling on HEIF output.
- **Quad Bayer DNG** — remosaic-to-2×2 Bayer path (direct 4×4 demosaic deferred to v1.2).

## Architecture

```
   DNG ──▶ Ingest ──▶ Pipeline DAG ──▶ Encoder ──▶ HEIF
                          │
                          ▼
                Scheduler  (TaskFlow + Device Plane)
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
           Compute                Inference
        (Halide / Slang)       (ExecuTorch / ONNX RT)
              │                       │
              ▼                       ▼
            Vulkan                HTP / ANE
              │                       │
              └───────────┬───────────┘
                          ▼
              IBuffer  (zero-copy across devices)


   Editor Server (HTTP + WS)  ◀────▶  Web Editor (React Flow, GitHub Pages)
```

Full diagram in [`docs/research/00-summary.md` §3](docs/research/00-summary.md#3-architecture-overview); module assembly in [`docs/architecture.md`](docs/architecture.md).

## Highlights

- **Heterogeneous compute & scheduling.** Pipelines are static DAGs that dispatch across CPU (Halide), GPU (Vulkan / Metal via Slang), and NPU (ExecuTorch on Hexagon HTP and Apple ANE) over zero-copy buffers (`AHardwareBuffer` / `IOSurface` / Vulkan external memory). Per-node precision is declared in the manifest; the scheduler inserts the minimum number of conversions, and topology is frozen after load with parameters remaining mutable.
- **Plugin model & visual editing.** Every node — first-party today, third-party in v2 — registers through a single C ABI + JSON manifest, collected at link time by `CPIPE_REGISTER_NODE`. A React Flow Web Editor (hosted on GitHub Pages) edits the graph live over loopback / LAN, with an offline JSON-only mode for users without a runtime.

## Current Status

**Phase 0** (`v0.1`) is released: the skeleton builds, tests, and runs the
passthrough pipeline. See [`docs/phase-00-foundation.md`](docs/phase-00-foundation.md).

**Phase 1** (`v0.2` — Walking Skeleton) is released: the synthetic and Pixel 8
Pro DNG → five-stage ISP → SDR HEIF CLI smokes are in place, the per-node
golden PSNR harness is green, and the integration HEIF re-decode PSNR gate is
green against the cpipe pre-HEIF reference. RawTherapee-derived references and
cpipe-owned Vulkan queue-dispatch proof slipped to P2. See
[`docs/phase-01-walking-skeleton.md`](docs/phase-01-walking-skeleton.md).

**Phase 2** (`v0.3` — Classic Nodes + HDR) is released: the runtime now carries
the classic-node surface (RCD / AMaZE / Quad Bayer remosaic; OpcodeList2
GainMap; OpcodeList3 lens correction; full dual-illuminant WB; BM3D / guided /
wavelet denoise; filmic / Mertens / ACES / Reinhard tone; 3D-LUT; edge-aware
USM), the HDR (PQ) HEIF output path (kvazaar Main10 + CICP 9/16/9 + mdcv /
clli + ICC v4.4), and OCIO v0.2 Looks (Standard SDR / Standard HDR). Foundation
work also retires the P1 Vulkan-dispatch slip
([P1-PD-71](docs/phase-01-walking-skeleton.md#4-phase-decisions-pd-n)), extends
the compute suite with parameter buffers + an OCIO host accessor, upgrades the
memory planner to interference-graph coloring, and switches the precision
planner to auto-insertion. Detail in
[`docs/phase-02-classic-nodes-hdr.md`](docs/phase-02-classic-nodes-hdr.md).

**Phase 3** (`v0.4` — Editor + Quality Harness) is in progress: detailed plan in
[`docs/phase-03-editor-iqa.md`](docs/phase-03-editor-iqa.md) (7 sub-phases, 24
T-tasks, 50 `P3-PD-N` rows locked). The phase delivers the Web Editor
(`apps/web/`, React Flow 12 + Zustand + Ajv), the in-process editor server
(`cpipe-server`, uWebSockets, 8 REST endpoints + WS thumbnail subscriptions),
the IQA harness (C++ in-binary PSNR / SSIM / MS-SSIM / ΔE2000 + Python sidecar
`tools/iqa/cpipe_iqa/` wrapping piq + pyiqa), the 50-image manifest-fetched v1
corpus, the microbench harness (`bench/`, Google Benchmark + nanobench), and
the Vega-Lite dashboard (`apps/dashboard/`). New CLI verbs: `cpipe serve / info
/ iqa / bench`. New schemas: `pipeline-v0.4.json` (optional `ui` object) and
`editor-protocol-v0.1.json`. P3 retires the OCIO Vulkan carry and adds
RawTherapee 5.10 tooling for the remaining golden-reference carry:
[P2-PD-74](docs/phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)
(OCIO Vulkan execution), plus `tools/golden/rt_render.sh` for the 7 RT 5.10
reference goldens ([P1-PD-69 / P1-PD-70](docs/phase-01-walking-skeleton.md#4-phase-decisions-pd-n)
+ partial [P2-PD-61 / P2-PD-62 / P2-PD-66 / P2-PD-67](docs/phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n));
checked-in RT-derived replacements remain carried by
[P3-PD-55](docs/phase-03-editor-iqa.md#4-phase-decisions-p3-pd-n).

## Build From Source

Requirements:

- Ubuntu 24.04 or equivalent Linux x86_64 environment.
- CMake 3.28+, Ninja, Clang 18+ or GCC 13+.
- vcpkg with `VCPKG_ROOT` pointing at the checkout.
- Python `pre-commit`.

```bash
pre-commit install
cmake --preset linux-debug
cmake --build --preset linux-debug -j
ctest --preset ci --output-on-failure
```

Release preset:

```bash
cmake --preset linux-release-clang
cmake --build --preset linux-release-clang -j
```

## Roadmap

| Tag    | Phase | Theme                                                       | Status      |
|--------|-------|-------------------------------------------------------------|-------------|
| `v0.1` | P0    | Foundation — repo skeleton, CI, plugin ABI, passthrough node | released    |
| `v0.2` | P1    | Walking skeleton — DNG → SDR HEIF on Linux through 5 nodes  | released; RawTherapee refs + full Vulkan proof slipped |
| `v0.3` | P2    | Classic + HDR — all 18 classic nodes; HDR HEIF (PQ); OCIO Looks; Quad Bayer remosaic | released; real QBC/RT refs + OCIO Vulkan/Tracy capture slipped |
| `v0.4` | P3    | Editor + IQA — React Flow editor, offline JSON mode, 50-image corpus, microbench harness | in progress; retires OCIO Vulkan; RT 5.10 golden replacement, real QBC, and Halide Vulkan AOT carry |
| `v0.5` | P4    | AI nodes — NAFNet-w32, AdaInt 3D-LUT, HDR+ Wronski burst    | planned     |
| `v1.0` | P5    | Polish — docs, sample plugin, golden refresh, RC bake, GA   | planned     |
| `v1.1` | —     | Android + HLG / UltraHDR / Apple Adaptive HDR + WSS / LNA / WebRTC / TURN editor connectivity | outlook |
| `v1.2` | —     | macOS native (Metal + IOSurface + ANE); AI demosaic for Quad Bayer | outlook |
| `v2`   | —     | External `.so` plugin loading; iOS; tile-based; ZSL; streaming preview | outlook |

Detail and RD-NN decisions: [`docs/roadmap.md`](docs/roadmap.md).

## Documentation

| Path                                                                | Purpose                                                                       |
|---------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [`docs/roadmap.md`](docs/roadmap.md)                                | Phase plan, RD-NN decision quick reference, v1.0 GA ship checklist            |
| [`docs/architecture.md`](docs/architecture.md)                      | System assembly: CMake targets, threading, lifecycle, editor protocol surface |
| [`docs/tech.md`](docs/tech.md)                                      | Every external dependency, version pin, and license verdict                   |
| [`docs/buffer.md`](docs/buffer.md)                                  | `IBuffer` subsystem; B1–B12 locked decisions; allocator + external imports    |
| [`docs/plugin-sdk.md`](docs/plugin-sdk.md)                          | Plugin C ABI (P1–P16); JSON manifest; `CPIPE_REGISTER_NODE` lifecycle         |
| [`docs/phase-00-foundation.md`](docs/phase-00-foundation.md)        | Phase 0 plan and outcome (shipped)                                            |
| [`docs/phase-01-walking-skeleton.md`](docs/phase-01-walking-skeleton.md) | Phase 1 plan and outcome (shipped)                                            |
| [`docs/phase-02-classic-nodes-hdr.md`](docs/phase-02-classic-nodes-hdr.md) | Phase 2 shipped evidence                                                       |
| [`docs/phase-03-editor-iqa.md`](docs/phase-03-editor-iqa.md) | Phase 3 plan in progress (`v0.4`)                                              |
| [`docs/research/_toc.md`](docs/research/_toc.md)                    | D1–D19 locked decisions; research cluster map; methodology                    |
| [`docs/research/00-summary.md`](docs/research/00-summary.md)        | Master research synthesis — recommended stack, cross-cluster matrix, risks    |

The 17 numbered chapters under [`docs/research/`](docs/research/) carry the evidence behind each recommendation.

## License

cpipe is licensed under the Apache License, Version 2.0. See [`LICENSE`](LICENSE).
Third-party licenses are catalogued in [`docs/tech.md`](docs/tech.md).

## Acknowledgements

Architecturally inspired by [vkdt](https://github.com/hanatos/vkdt) (BSD-2). Runtime uses Halide, Slang + slang-rhi, TaskFlow, ExecuTorch, ONNX Runtime, LibRaw, OpenColorIO, lcms2, libheif, kvazaar, libde265, libultrahdr, and React Flow, among others. See [`docs/tech.md`](docs/tech.md) for the full inventory.
