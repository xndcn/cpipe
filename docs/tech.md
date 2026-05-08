# cpipe Tech Stack

> Date: 2026-05-08 · Companion: [`architecture.md`](architecture.md) · Source of truth for evidence: [`research/00-summary.md`](research/00-summary.md)

This document is the **shopping list** for cpipe v1: every external library, language, and tool we depend on, the version pinned for v1, the license verdict against [D11 Apache 2.0](research/_toc.md#1-decisions-locked-before-research), and a one-line link to the research chapter that justifies the choice. It does **not** explain how the pieces fit together — that lives in [`architecture.md`](architecture.md).

The dependency graph is structured by layer, mirroring [`research/00-summary.md §2`](research/00-summary.md#2-recommended-stack-at-a-glance). Each row in the per-layer tables lists: the choice, the verified version as of 2026-05-08, the SPDX license tag, and the research chapter that owns the decision. **Rejected candidates** (x265, Diligent, LiteRT, …) are out-of-band — see [`research/00-summary.md §2`](research/00-summary.md#2-recommended-stack-at-a-glance) for the one-liners and the matching chapters for the deep evaluation.

---

## 1. Layer Map

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 10  Web Editor Frontend             (React Flow / Vite)   │
│ Layer  9  Editor Server / Connectivity    (uWebSockets / Noise) │
│ Layer  8  Mobile Capture                  (Camera2 NDK)         │
│ Layer  7  Plugin & Manifest               (custom + nlohmann)   │
│ Layer  6  Color & Format Output           (libheif / OCIO)      │
│ Layer  5  ISP Algorithms                  (re-implemented)      │
│ Layer  4  AI Inference & NPU              (ExecuTorch / QAIRT)  │
│ Layer  3  Scheduler & Observability       (TaskFlow / Tracy)    │
│ Layer  2  Compute Foundation              (Halide / Slang)      │
│ Layer  1  Build & Toolchain               (CMake / vcpkg)       │
└─────────────────────────────────────────────────────────────────┘
```

`architecture.md` walks the layers top-down (how a request travels). This document walks them bottom-up (what each layer is built on).

---

## 2. Layer 1 — Build & Toolchain

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Language | C++20 (native) / TypeScript 5 (web) / Kotlin 2.0 (Android UI only) | — | — | [D18](research/_toc.md#1-decisions-locked-before-research) / [D19](research/_toc.md#1-decisions-locked-before-research) |
| Build system | **CMake** | 3.28+ | BSD-3 | [D18](research/_toc.md#1-decisions-locked-before-research) |
| Presets | **CMakePresets.json** | schema v6 | n/a | Reproducible developer builds across Linux x86_64 / Android arm64 |
| Dependency manager | **vcpkg** (manifest mode) | baseline pinned per release | MIT | [D18](research/_toc.md#1-decisions-locked-before-research) |
| In-tree fetch | **CMake FetchContent** | n/a | n/a | Used for libraries vcpkg lacks (e.g. Halide HEAD pins, libultrahdr 1.4 patches) |
| Overlay ports | `vcpkg/overlay-ports/` | n/a | per-port | Local patches for kvazaar, libultrahdr until upstream catches up |
| Compiler — Linux | clang ≥ 18, gcc ≥ 13 | — | — | C++20 modules and `<expected>` support |
| Compiler — Android | NDK r27 clang | r27 (2026 LTS) | NCP | AGP 8.7 default toolchain |
| Format | **clang-format** | 18+ | Apache 2.0 + LLVM | Project style file in repo root |
| Lint | **clang-tidy** | 18+ | Apache 2.0 + LLVM | Enforced in CI; ruleset in `.clang-tidy` |
| CI | **GitHub Actions** | runners 2026-05 | n/a | Linux x86_64 + Android arm64 cross-compile + Pages deploy + nightly IQA cron |
| Test runner | **Catch2 v3** | 3.6+ | BSL-1.0 | [Research 09](research/09-image-quality-benchmarks.md) |
| Microbenchmark | **Google Benchmark** + **nanobench** | latest | Apache 2.0 / MIT | [Research 09](research/09-image-quality-benchmarks.md) |
| Profiling — runtime | **Tracy** + **Perfetto SDK** + Chrome Trace JSON | Tracy 0.11+, Perfetto 47+ | BSD-3 / Apache 2.0 | [Research 03 §11](research/03-heterogeneous-scheduler.md) — disabled by default; `CPIPE_ENABLE_TRACY=ON` flips on |

vcpkg discipline: every release branch ships a `vcpkg.json` whose `builtin-baseline` is a Microsoft vcpkg-registry commit; ports that need a newer version than the registry holds live in `vcpkg/overlay-ports/<name>/` with our patches under `patches/`. Halide and Slang are FetchContent because their tag cadence outpaces vcpkg. Build presets bind compiler, sanitizer, and toolchain in one place; developers run `cmake --preset=linux-debug` rather than memorising flags.

---

## 3. Layer 2 — Compute Foundation

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Portable algorithm language | **Halide** | v21.0.0 (2025-09-16) | MIT | [Research 01](research/01-compute-frameworks.md) |
| Hand-tuned shader language | **Slang** | v2026.8 | Apache-2-LLVM | [Research 01](research/01-compute-frameworks.md) |
| Shader RHI | **slang-rhi** | HEAD `cc6742b0` (2026-05-07) | MIT | [Research 01](research/01-compute-frameworks.md); pin to commit, not tag |
| GPU API — Linux desktop | **Vulkan 1.3** + opaque-fd external memory | Mesa 25 / NVIDIA 570+ | n/a | [Research 02](research/02-zero-copy-buffer-architecture.md) |
| GPU API — Android | **Vulkan 1.3** + `VK_ANDROID_external_memory_AHB` | Android API 35 | n/a | [Research 02](research/02-zero-copy-buffer-architecture.md) |
| GPU API — Apple (v2) | **Native Metal 3** (no MoltenVK) | macOS 14+ / iOS 17+ | n/a | [Research 02](research/02-zero-copy-buffer-architecture.md) — [D18](research/_toc.md#1-decisions-locked-before-research) excludes MoltenVK |
| GPU memory allocator | **VulkanMemoryAllocator** (VMA) | latest | MIT | [Buffer §7](buffer.md#71-internal-allocator-vma--mtlheap) |
| Metal allocator (v2) | `MTLHeap` | platform | Apple SDK | [Buffer §7](buffer.md#71-internal-allocator-vma--mtlheap) |

The two-language pattern is deliberate: Halide handles auto-scheduled, math-heavy nodes with a single source compiled to CPU/Vulkan/Metal/Hexagon HVX; Slang covers the cases where we need hand-tuned compute shaders with explicit subgroup tricks. They cohabit through the host-side `ComputeContext` ([Plugin SDK §9](plugin-sdk.md#9-compute-backend-submission-protocols)).

---

## 4. Layer 3 — Scheduler & Observability

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Task graph | **TaskFlow** | v4.0.0 (2026-01-02) | MIT | [Research 03](research/03-heterogeneous-scheduler.md) |
| Logging | **spdlog** | 1.14+ | MIT | Structured async sinks; integrates with Tracy text spans |
| String formatting | `fmt` | bundled with spdlog | MIT | Pulled transitively |
| Tracing | see Layer 1 (Tracy + Perfetto + Chrome Trace JSON) | — | — | — |

We do not pull in any device plane library. The scheduler ships as part of `cpipe-runtime` and is described in [`architecture.md`](architecture.md) and [Research 03](research/03-heterogeneous-scheduler.md).

---

## 5. Layer 4 — AI Inference & NPU

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Inference engine — primary | **ExecuTorch** | 1.2.0 (2026-04-01) | Apache 2.0 | [Research 04](research/04-mobile-ai-inference.md) |
| Inference engine — escape hatch | **ONNX Runtime** | 1.25.1 (2026-05) | MIT | [Research 04](research/04-mobile-ai-inference.md) |
| NPU — Qualcomm | **QAIRT** (`libQnnHtp.so`) | 2.43 | Proprietary EULA, dynamic-link only | [Research 05](research/05-npu-backends-zero-copy.md) |
| NPU — Apple (v2) | **Core ML 8** + ANE via `MLProgram` | iOS 19 / macOS 16 | Apple platform SDK | [Research 05](research/05-npu-backends-zero-copy.md) |
| NPU — MediaTek / Samsung | not in v1 | — | — | [D13](research/_toc.md#1-decisions-locked-before-research) |
| Model assets | local `models/` directory; manual fetch | — | per-model | First-party scripts under `scripts/fetch_models.py`; default offline |

The host links inference SDKs; **plugins never link them**. See [Plugin SDK §9.3](plugin-sdk.md#93-inference) and [Architecture · Inference Submission](architecture.md).

---

## 6. Layer 5 — ISP Algorithms

Algorithms are not third-party libraries — every classic and AI node listed in [Research 07](research/07-classic-isp-algorithms.md) and [Research 08](research/08-ai-isp-algorithms.md) is **re-implemented under Apache 2.0** because their canonical FOSS implementations are GPL ([D11](research/_toc.md#1-decisions-locked-before-research) trap). Only the supporting libraries are external:

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| DNG ingest | **LibRaw** | 0.22 | LGPL 2.1 with static-link clause | [Research 12](research/12-dng-format.md) |
| OpcodeList interpreter | first-party (~1.5 kLoC under `cpipe::ingest::dng_opcode`) | n/a | Apache 2.0 | LibRaw does not parse OpcodeList |
| Lens correction database | **Lensfun** library + DB | latest | LGPL 3 + DB CC-BY-SA | Dynamic-link the library; DB licence applies to data only |
| Image I/O for tests / debug dumps | **OpenImageIO** | 2.5 | BSD-3 | EXR + PNG read/write for golden-image fixtures |

Inspirations that we **do not link** (study-only): vkdt (BSD-2 — safe to read), Adobe DNG SDK (Adobe EULA — pending Q1 legal review), HDR+ IPOL re-implementation (paper + code reference). Traps explicitly avoided: x265, exiv2, dt-dng, darktable / RawTherapee cores, dcraw, Argyll CMS, DCamProf, OpenCamera. Full inventory: [Research 00 §6](research/00-summary.md#6-license-inventory).

---

## 7. Layer 6 — Color & Format

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Working color space | linear Rec.2020 D65 (FP16) | n/a | n/a | [Research 13](research/13-color-management.md) |
| Pipeline color transforms | **OpenColorIO** | 2.4+ | BSD-3 | [Research 13](research/13-color-management.md) |
| ICC | **lcms2** | 2.16+ | MIT | [Research 13](research/13-color-management.md) |
| HEIF container | **libheif** | 1.20.1+ | LGPL + commercial | [Research 14](research/14-heif-and-hdr-output.md) — dynamic link |
| HEVC encode | **kvazaar** (avoid x265 GPL trap) | 2.3+ | BSD-3 | [Research 14](research/14-heif-and-hdr-output.md) |
| HEVC decode (preview) | **libde265** | 1.0.16+ | LGPL | dynamic link |
| UltraHDR | **libultrahdr** | 1.4+ | Apache 2.0 | [Research 14](research/14-heif-and-hdr-output.md) |
| Calibration profile | first-party `.cpcal` JSON | n/a | Apache 2.0 | [Research 15](research/15-mobile-camera-calibration.md); Adobe DCP write deferred (Q4) |

OCIO drives in-pipeline transforms; lcms2 reads / writes ICC profiles for HEIF embedding. CICP signalling (`9 / 16 / 9` for BT.2020-PQ; `9 / 18 / 9` for BT.2020-HLG) is written by libheif when configured by `cpipe::color::HeifWriter`.

---

## 8. Layer 7 — Plugin & Manifest

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Plugin ABI | first-party `cpipe_node.h` v0.1 | — | Apache 2.0 | [Plugin SDK](plugin-sdk.md) |
| C++ SDK | first-party `cpipe/sdk.hpp` | — | Apache 2.0 | [Plugin SDK §6](plugin-sdk.md#6-c-sdk-cpipesdkhpp) |
| JSON | **nlohmann/json** | 3.11+ | MIT | [Plugin SDK §7.3](plugin-sdk.md#73-validation) |
| JSON Schema validation — host | **nlohmann/json-schema-validator** | latest | MIT | [Plugin SDK §7.3](plugin-sdk.md#73-validation) |
| JSON Schema validation — editor | **Ajv** | 8.x | MIT | [Plugin SDK §7.3](plugin-sdk.md#73-validation) |
| Manifest schema version | `https://schemas.cpipe.dev/node/v0.1.json` | v0.1 (alpha) | n/a | embedded into the binary; served at `/api/schemas/node` |

Halide AOT artifacts are produced by `add_halide_library()` per node and statically linked into `cpipe-builtin-nodes`. Slang shaders are compiled per node into a multi-target `.slangbin` (SPIR-V + MSL + WGSL TLV archive) shipped under `share/cpipe/slang/`.

---

## 9. Layer 8 — Mobile Capture

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Capture API | **Camera2 NDK** | API 35 | Android platform | [Research 16](research/16-camera2-raw-and-burst.md) — CameraX 1.5 abstracts away `captureBurst` |
| Image reader | `AImageReader_newWithUsage` | platform | Android platform | needs `AHARDWAREBUFFER_USAGE_GPU_*` |
| Buffer interop | `AHardwareBuffer` ↔ Vulkan via `VK_ANDROID_external_memory_AHB` | platform | Android platform | [Buffer §6](buffer.md#6-concrete-backend-types) |
| DNG writer | first-party (not `DngCreator`) | — | Apache 2.0 | needed for canonical OpcodeList output |
| UI | **Jetpack Compose** + Material 3 | Compose 2026.05 | Apache 2.0 | [D19](research/_toc.md#1-decisions-locked-before-research) |
| Build | **Gradle** + AGP | AGP 8.7 + Gradle 8.13 | Apache 2.0 | invokes CMake/NDK twin-track build |

---

## 10. Layer 9 — Editor Server & Connectivity

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| HTTP+WebSocket server | **uWebSockets** | v20.62+ | Apache 2.0 | low-overhead, integrates with TaskFlow event loop |
| Pairing crypto | **libsodium** + **noise-c** | 1.0.20 / latest | ISC / BSD-2 | Noise XK protocol per [Research 11 §6](research/11-pipeline-editor-and-connectivity.md) |
| TLS — LAN-cert path | OS-supplied (system trust store) | platform | various | [Research 11 §5](research/11-pipeline-editor-and-connectivity.md) |
| Signalling for WebRTC fallback | Cloudflare Workers (deployed separately) | — | n/a | [Research 11 §5](research/11-pipeline-editor-and-connectivity.md) |
| TURN | Cloudflare TURN | — | n/a | last-resort fallback |

Wire format: JSON for the control plane, framed binary (1 B type + 4 B node-id + 4 B timestamp + 4 B length + payload) for thumbnail and profile streams. Protocol detail: [`architecture.md` · Editor Server](architecture.md).

---

## 11. Layer 10 — Web Editor Frontend

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| Build tool | **Vite** | 6.x | MIT | [Research 11](research/11-pipeline-editor-and-connectivity.md) |
| UI library | **React** | 19.x | MIT | React Flow targets React 18+ |
| Language | **TypeScript** | 5.x | Apache 2.0 | strict mode |
| Graph component | **React Flow** (`@xyflow/react`) | 12.10.x | MIT | [Research 11](research/11-pipeline-editor-and-connectivity.md) |
| State | **Zustand** + persist middleware | latest | MIT | [Research 11](research/11-pipeline-editor-and-connectivity.md) |
| Schema validation | **Ajv** | 8.x | MIT | matches host-side validator |
| Package manager | **npm** | 10+ | Artistic-2.0 | repository default; lockfile committed |
| Hosting | **GitHub Pages** | — | n/a | static export from `apps/web/dist/` |
| Deploy | GitHub Actions → `gh-pages` branch | — | — | triggered on `apps/web/**` changes |

In-browser node *authoring* (writing Halide / Slang code from the editor) is **not in v1**. The editor edits `pipeline.cpipe.json` only — node code lives in the cpipe repo. See `architecture.md` · Editor Scope.

---

## 12. Layer 11 — Quality & Observability

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| IQA — in-binary | **piq** | latest | Apache 2.0 | [Research 09](research/09-image-quality-benchmarks.md) — covers PSNR/SSIM/MS-SSIM/GMSD/ΔE2000/BRISQUE |
| IQA — subprocess | **pyiqa** | latest | NTU + CC-BY-NC-SA | [Research 09](research/09-image-quality-benchmarks.md) — CI-only; license-isolated via subprocess |
| Statistical gate | Wilcoxon signed-rank (in piq) | n/a | Apache 2.0 | corpus-level gate per [Research 09](research/09-image-quality-benchmarks.md) |
| Test corpus | 50 curated DNG/EXR pairs from FiveK + SIDD + DND + HDR+ + RAISE + Kalantari + Quad Bayer phone shots | — | per-source | stored under `tests/corpus/` (Git LFS) |
| Golden image format | OpenEXR (linear Rec.2020 FP16) per node | — | BSD-3 | Git LFS |
| Perf bench | **Google Benchmark** + nanobench | latest | Apache 2.0 / MIT | recorded but not gated in CI |
| Dashboard | Vega-Lite + GitHub Pages | — | BSD-3 | published alongside the editor |

---

## 13. CLI & Runtime Utilities

| Component | Choice | Version | License | Why |
|-----------|--------|---------|---------|-----|
| CLI parsing | **CLI11** | 2.4+ | BSD-3 | subcommand structure (`cpipe run / bench / info / serve / model`) |
| Filesystem | `std::filesystem` (C++20) | — | — | — |
| Threading primitives | `std::thread` / `std::condition_variable_any` | — | — | TaskFlow handles graph-level scheduling |
| `Result<T>` | `std::expected` (C++23) with `tl::expected` polyfill | — | Apache 2.0 / MIT | [Plugin SDK §10](plugin-sdk.md#10-error-handling-result-style) |

---

## 14. Versioning Policy

- **Pin to `major.minor`** for every entry above. The `vcpkg.json` `builtin-baseline` and overlay ports record the exact `version-date` pin per release branch.
- **Pin to commit** for `slang-rhi` only; its tag cadence is too slow.
- **Bump policy** — minor version moves are reviewed at release-branch cut; major moves require an ADR-style PR referencing the affected research chapter and an IQA-corpus regression run.
- **Cpipe ABI** is independent of dependency versioning. See [Plugin SDK §4](plugin-sdk.md#4-abi-versioning) — currently v0.1 alpha.

---

## 15. License Posture (cliff-notes)

cpipe is **Apache 2.0**. The three traps every contributor must remember:

1. **x265 is GPLv2** — encode through kvazaar instead.
2. **exiv2 / dt-dng / darktable core / RawTherapee core / dcraw / DCamProf / OpenCamera / FreeDcam are all GPL** — read for inspiration, do not copy or link.
3. **pyiqa is CC-BY-NC-SA** — must run in a separate process; never embed in the binary.

The full per-dependency verdict table is [`research/00-summary.md §6`](research/00-summary.md#6-license-inventory). When adding any new dependency, update that table first; this document follows.

---

## 16. Rejected Candidates

[`research/00-summary.md §2`](research/00-summary.md#2-recommended-stack-at-a-glance) lists every alternative we evaluated and the chapter that explains why it was rejected. The headline rejections — Diligent Engine (commercial-only Metal backend), Kompute (Vulkan-only), NVRHI (no Metal), Dawn / wgpu-native (build complexity, WGSL-first), LiteRT (Hexagon delegate + NNAPI both deprecated in 2026), TVM / IREE (mobile maturity), x265 (GPL) — appear in the headers of [Research 01](research/01-compute-frameworks.md) and [Research 04](research/04-mobile-ai-inference.md).

---

## 17. See Also

- [`architecture.md`](architecture.md) — how the pieces in this document fit together.
- [`buffer.md`](buffer.md) — `IBuffer` / VMA / external imports / synchronization.
- [`plugin-sdk.md`](plugin-sdk.md) — `cpipe_node.h`, manifest schema, lifecycle.
- [`research/00-summary.md`](research/00-summary.md) — master synthesis with full evidence trail.
- [`research/_toc.md`](research/_toc.md) — locked decisions D1–D19 and out-of-scope list.
