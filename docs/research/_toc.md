# cpipe Research Package — Table of Contents

`cpipe` (Computational Photography Pipeline) is a professional camera/post-processing system built around a **DAG-based soft ISP** that runs on **CPU + GPU + NPU** with **zero-copy** buffers, exposes **plugin-based nodes** (classic + AI), and is editable through a **node web editor**.

This document indexes a deep-research package whose purpose is to produce an **implementation-ready** architecture by surveying 2024–2026 prior art and best practices.

---

## 1. Decisions Locked Before Research

These decisions were locked through structured Q&A with the human before research began. All sub-agents and reports treat them as **hard constraints** and must call them out if research uncovers a violation.

| ID  | Decision                       | Value                                                                                                                                                                              |
|-----|--------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| D1  | v1 deliverable                 | Linux desktop **CLI** (read DNG → write HEIF) + **Web Editor** (React Flow, hosted on GitHub Pages); Android app is later in v1 (Camera2 RAW); macOS / iOS = v2.                   |
| D2  | Max input image size           | **100 MP** single-plane Bayer; tile-based processing **deferred to v2**. Single intermediate buffer @ FP32 ≈ 800 MB (worst case); architecture must respect this peak.             |
| D3  | Multi-frame model              | **Burst-on-shutter** (5–10 frames per shot). ZSL ring buffer, video, and streaming preview deferred.                                                                              |
| D4  | Plugin distribution v1         | **Built-in only**. C ABI is *internal* architecture; v1 does not load external `.so` from user space. Architecture must be ABI-clean so v2 can.                                    |
| D5  | Streaming / preview            | **Batch-only** v1. Buffer / scheduler design must not paint itself into a corner that blocks streaming, but only batch is implemented.                                            |
| D6  | DAG dynamism                   | **Static topology after load**. Parameters mutable; nodes / edges fixed at runtime. (No conditional sub-graphs in v1.)                                                            |
| D7  | HDR output                     | SDR HEIF + **HDR HEIF (PQ / HLG)** + **Google UltraHDR** (gain-map) + **Apple Adaptive HDR**.                                                                                     |
| D8  | Color management               | **ICC profiles + OpenColorIO** (cinema-grade). Working color space chosen by research; must justify against linear ProPhoto / linear Rec.2020 / ACEScg.                            |
| D9  | Precision policy               | **FP16 default** on GPU. Each node declares input / output precision in its **manifest**; scheduler inserts the minimum number of conversion ops.                                  |
| D10 | Calibration v1                 | **DNG metadata only** (ColorMatrix1/2, ForwardMatrix1/2, AsShotNeutral, NoiseProfile, OpcodeList1–3, Black/White Level). Architecture **reserves** a custom-calibration API for v2. |
| D11 | License                        | **Apache 2.0**. Hard-blocks GPLv3-licensed dependencies (e.g. RawTherapee core, darktable core); blocks AGPL services entirely.                                                    |
| D12 | Bayer patterns                 | RGGB / BGGR / GRBG / GBRG **+ Sony / Samsung Quad Bayer** (Tetra / Quadra). X-Trans deferred.                                                                                     |
| D13 | NPU priority                   | **Qualcomm Hexagon** (Android flagship) + **Apple Neural Engine** (Apple platforms). MediaTek APU / Samsung NPU mentioned but not deeply researched.                              |
| D14 | Timeline                       | **~12 months for v1**. Research depth is generous; architecture should be production-grade.                                                                                       |
| D15 | Stack candidates (NOT locked)  | slang + slang-rhi, Halide, ExecuteTorch, ONNX Runtime, TaskFlow are **starting candidates**. Research **may** recommend alternatives; report must justify.                         |
| D16 | Editor connectivity            | LAN HTTP+WebSocket **+ P2P / relay** (NAT traversal in scope; STUN / TURN / WebRTC DataChannel research expected).                                                                |
| D17 | Save format                    | **HEIF only** (libheif or platform-native). No JPEG fallback in v1. AVIF may be evaluated as comparison.                                                                          |
| D18 | Build system                   | **CMake + C++20 + vcpkg + FetchContent**. No Bazel / Buck / Conan. Cross-compile target: Linux x86_64 (v1), Android arm64 (v1), macOS / iOS (v2).                                  |
| D19 | UI language layering           | **Native (C/C++)** for everything that is not platform UI. **Kotlin** for Android UI. Platform abstraction layer between them.                                                    |

---

## 2. Research Cluster Map

Sub-agents are clustered by **decision interaction** (decisions in the same cluster influence each other; running them together avoids inconsistent recommendations). **Reports are split by independent topic** with cross-references — readers can navigate by interest.

| Cluster | Sub-agent focus                                              | Reports it owns      |
|---------|--------------------------------------------------------------|----------------------|
| **A**   | **Compute Foundation** — GPU API, zero-copy, scheduler       | 01, 02, 03           |
| **B**   | **AI Inference & NPU** — engines, NPU SDKs, GPU↔NPU interop  | 04, 05               |
| **C**   | **ISP Pipeline & Algorithms** — soft ISP, classic+AI, IQA    | 06, 07, 08, 09       |
| **D**   | **Plugin & Pipeline Editor** — ABI, node UI, NAT traversal   | 10, 11               |
| **E**   | **Color / Format / Calibration** — DNG, color, HEIF, calib   | 12, 13, 14, 15       |
| **F**   | **Camera2 & Mobile Pro Apps** — capture path, prior art      | 16, 17               |

Decisions interact across clusters (e.g. NPU choice in Cluster B depends on zero-copy fabric in Cluster A; ISP algorithms in Cluster C drive precision policy in Cluster A). Each sub-agent receives the **full lock list (Section 1)** and a **reading list of sibling clusters' starting candidates**, so cross-cluster impacts are surfaced and called out, even though each agent owns its own report files.

After all sub-agents return, the orchestrator writes **00-summary.md**, the master synthesis. Cross-cluster conflicts are resolved there.

---

## 3. Reports

All reports live in `docs/research/`. Each report follows this structure and **targets 5,000–6,000 words** (excluding code blocks and reference URLs):

```
1. TL;DR                           (≤ 200 words)
2. Decision matrix                 (recommendations + trade-off table)
3. Detailed findings               (long-form, sectioned, the bulk of the body)
4. Concrete code / architecture sketches (where useful)
5. Cited sources                   (URLs, GitHub repos, papers, version + date)
6. Cross-references ("See also")
7. Open questions                  (things research could not settle)
```

| #  | File                                              | Topic                                                                                            |
|----|---------------------------------------------------|--------------------------------------------------------------------------------------------------|
| 00 | `00-summary.md`                                   | Master synthesis: recommended stack, architecture diagram, risk register, decision matrix       |
| 01 | `01-compute-frameworks.md`                        | slang-rhi, Dawn / WebGPU, Vulkan, Halide, Diligent Engine, Kompute — survey + recommendation     |
| 02 | `02-zero-copy-buffer-architecture.md`             | AHardwareBuffer, IOSurface, Vulkan external memory, fences, timeline semaphores                  |
| 03 | `03-heterogeneous-scheduler.md`                   | TaskFlow 4.0 vs alternatives; CPU / GPU / NPU coordination; zero-copy hand-off across devices    |
| 04 | `04-mobile-ai-inference.md`                       | ExecuteTorch, ONNX RT, MNN, NCNN, TFLite, Core ML — coverage, latency, memory                    |
| 05 | `05-npu-backends-zero-copy.md`                    | Qualcomm QNN / SNPE / Hexagon NN; Apple Core ML / ANE; GPU↔NPU shared-memory paths               |
| 06 | `06-soft-isp-architectures.md`                    | vkdt, darktable, RawTherapee, ART, Adobe Camera Raw — architecture lessons learned               |
| 07 | `07-classic-isp-algorithms.md`                    | Demosaic (Bayer + Quad Bayer), WB, tone, denoise, sharpen, lens correction, multi-frame fusion   |
| 08 | `08-ai-isp-algorithms.md`                         | AI demosaic, AI denoise (NAFNet, Restormer, …), AI HDR, Burst Photography, AI tone               |
| 09 | `09-image-quality-benchmarks.md`                  | IQA-PyTorch, MANIQA, TOPIQ, NIQE / BRISQUE; full-ref vs no-ref; benchmark harness design         |
| 10 | `10-plugin-architecture.md`                       | ComfyUI, OFX / OpenFX, OBS, GStreamer, Blender add-on; C ABI design + C++ SDK pattern            |
| 11 | `11-pipeline-editor-and-connectivity.md`          | React Flow, LiteGraph; WebSocket, WebRTC DataChannel, STUN / TURN; UltraHDR-aware preview        |
| 12 | `12-dng-format.md`                                | DNG 1.7 spec, TIFF/EP, OpcodeList1–3, metadata extraction; libraw, exiv2, dng_sdk                |
| 13 | `13-color-management.md`                          | ICC v4, OpenColorIO 2.x, working color spaces, gamut mapping, HDR gamut handling                 |
| 14 | `14-heif-and-hdr-output.md`                       | libheif, x265 / libde265 / kvazaar; HDR HEIF (PQ / HLG, CICP); UltraHDR; Apple Adaptive HDR      |
| 15 | `15-mobile-camera-calibration.md`                 | Color calibration (Macbeth), vignetting, lens distortion, noise model; DNG OpcodeList semantics  |
| 16 | `16-camera2-raw-and-burst.md`                     | Camera2 RAW10/16/PRIVATE, ImageReader sizing, burst capture, AHardwareBuffer producer surfaces   |
| 17 | `17-mobile-pro-camera-apps.md`                    | Halide app (Lux), Adobe Lightroom Mobile, ProCamera, Moment Pro — UX + architecture lessons      |

---

## 4. Cross-Reference Conventions

- Reports cite each other inline as `[#NN — short title](NN-name.md#section)`.
- Every report ends with a **See also** section.
- The summary (`00`) cites every chapter and exposes a single recommended architecture diagram.
- Where reports recommend a library, they cite a **specific commit / tag / release date** so future readers can verify currency.

## 5. Methodology Each Sub-Agent Follows

1. Source 2024–2026 material first (web search and `gh search repos / issues`); cite older sources only when canonical.
2. Inspect at least **3 reference repositories** for any major architectural choice; record commit hashes.
3. Verify version numbers cited in [Section 1, D15] against current releases as of **2026-05-08**. If a version (e.g. "Halide v21.0") does not exist or has known issues, flag and recommend an actually-released alternative.
4. Quantify trade-offs (latency, memory, supported platforms, license).
5. Provide concrete sketches: signature, architecture diagram, or pseudocode where it speeds implementation.
6. Cite all sources with full URL and date.
7. Surface **open questions** that research could not resolve — these become next-round questions for the human.
8. **Word-count target: 5,000–6,000 words per report.** Use depth, not padding — if a topic genuinely closes in 3,000 words, say so and use the saved space to surface open questions or add a sketch. If a topic legitimately needs 7,000 words, do that and call out which sections were over-budget.
9. Do **not** discuss DMA-BUF or Linux dma-buf-specific paths in the zero-copy material; the architecture relies on AHardwareBuffer / IOSurface / Vulkan external memory abstractions instead.

## 6. Out of Scope

These are **explicitly not researched** in this round (per locked decisions):

- Tile-based / out-of-core image processing (deferred until v2).
- Live preview pipeline / streaming dataflow scheduler (D5).
- Zero-shutter-lag (ZSL) ring buffer (D3).
- iOS / macOS implementation specifics beyond what's needed to keep the architecture portable (timeline).
- Plugin marketplace / signing / sandboxing (D4).
- X-Trans demosaic (D12).
- MediaTek APU / Samsung NPU deep-dive (D13).
- Video codec output (HEVC / AV1 video).
- JPEG / AVIF as primary output (D17).
