# Phase 1 — Walking Skeleton

> Date: 2026-05-11 · Phase tag: `v0.2` · Parent: [`roadmap.md`](roadmap.md) · References: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`phase-00-foundation.md`](phase-00-foundation.md)

This is the detailed plan for Phase 1 of the cpipe v1.0 roadmap. P1's purpose is to turn a real Bayer DNG (Pixel 8 Pro) into an SDR HEIF on Linux x86_64 through a five-stage ISP pipeline, while standing up every cross-cutting subsystem that subsequent phases compound on: DngReader + opcode-list metadata parser, working-color-space plumbing (OCIO + lcms2), a libheif/kvazaar SDR HEIF writer, TaskFlow-driven parallel topological dispatch with interference-graph memory planning, a Vulkan device plane minimum (linearize node via Halide Vulkan AOT), Tracy instrumentation, and a golden-image harness in Git LFS.

When P1 is done, the project is tagged `v0.2` and Phase 2 begins.

---

## 1. Objective

A Pixel 8 Pro Bayer DNG (DNG 1.4–1.6, RGGB / BGGR / GRBG / GBRG) becomes an SDR 8-bit HEIF main yuv444p through five canonical ISP stages on Linux x86_64:

```
DNG file ──► DngReader (host) ──► linearize ──► blacklevel ──► wb.dual_illuminant ──► demosaic.bilinear ──► colormatrix.dng_to_working ──► heif_sdr ──► HEIF file
                                  (Vulkan)      (CPU)          (CPU)                  (CPU)                  (CPU)                          (CPU)
```

**Success looks like:**
- `cpipe run pixel8pro_d65.dng -p min-pipeline.cpipe.json -o out.heif` exits 0 on a real public-domain Pixel 8 Pro DNG and produces a HEIF that loads in GNOME Image Viewer / Eye of GNOME / macOS Preview.
- Golden EXR fixtures for every node + one end-to-end golden HEIF pass `ctest --preset golden`; PSNR ≥ 40 dB vs the pinned RawTherapee-bilinear reference; the end-to-end output is within ± 3 dB of the same reference (see [PD-63](#4-phase-decisions-pd-n)).
- TaskFlow Executor dispatches the DAG in parallel (linearize on Vulkan, the rest on CPU Halide AOT) with VkFence + binary VkSemaphore + timeline VkSemaphore wiring linearize → blacklevel.
- Tracy zones compile in when `CPIPE_ENABLE_TRACY=ON`; default is OFF.
- Manifest schema bumped to `node-v0.2.json` / `pipeline-v0.2.json` (caps / precision / color fields); the C ABI minor stays `0.1` (no suite added).
- All eight roadmap §4 sub-domains land in the same phase ([PD-32](#4-phase-decisions-pd-n)).

P1 explicitly does *not* deliver RCD/AMaZE demosaic, OpcodeList3 lens correction, Quad Bayer, HDR HEIF, the Editor, IQA harness, the AI nodes, or any non-Linux target — those are P2 and later.

---

## 2. Inputs

- P0 outputs (`v0.1`): six CMake targets compile, plugin ABI header, registry walk, passthrough node, 25 local Catch2 tests, green `build-and-test.yml`.
- Per [PD-78](#4-phase-decisions-pd-n) phase-01 records P0 closure state inherited from `phase-00-foundation.md` PD-31 ("24 h CI wait elapsed by maintainer instruction"); P1 does not re-verify P0.
- Locked design documents in `docs/research/`, `docs/architecture.md`, `docs/buffer.md`, `docs/plugin-sdk.md`, `docs/tech.md`, `docs/roadmap.md`, with P1-only patches landing in T1 (see [PD-82](#4-phase-decisions-pd-n) / [PD-83](#4-phase-decisions-pd-n)).
- Public-domain Pixel 8 Pro Bayer DNG sample from [raw.pixls.us](https://raw.pixls.us/) (CC-BY) committed under `tests/corpus/` via Git LFS.

---

## 3. Outputs

- Tag `v0.2` on the green build that satisfies the DoD in [§10](#10-definition-of-done-verification-commands).
- The repository layout described in [§5](#5-repository-layout-p1-deltas-vs-p0).
- A `cpipe` CLI binary on Linux x86_64 that turns a Bayer DNG into an SDR HEIF.
- Golden EXR fixtures per node (5 fixtures) + one end-to-end HEIF golden under `tests/golden/`.
- Updated `roadmap.md` (Phase Status table → P1 *In Progress* during the phase, *Released* at tag), `README.md` (Current Status), `architecture.md` (§11 example + §17 Q15 note), `buffer.md` (PixelFormat extension).

---

## 4. Phase Decisions (PD-N)

P1-specific decisions, locked from the planning Q&A. Where a P1 decision narrows a roadmap-level [RD-NN](roadmap.md#1-decision-quick-reference) or extends a P0-level PD, that link is cited. PD numbering continues from P0's PD-31.

| ID    | Decision                                  | Value                                                                                                                                                              |
|-------|-------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| PD-32 | P1 scope                                  | **Maximal** — all eight sub-domains from [`roadmap.md` §4](roadmap.md#4-phase-1--walking-skeleton-tag-v02) land in P1.                                              |
| PD-33 | P1 tag                                    | **`v0.2`**, per [RD-16](roadmap.md#1-decision-quick-reference).                                                                                                     |
| PD-34 | Task slicing                              | Twelve vertical tasks (T1–T12) and three checkpoints (after T4 / T9 / T12). See [§6](#6-task-list).                                                                |
| PD-35 | LibRaw integration                        | **vcpkg port** with **dynamic linking** (runtime `dlopen libraw.so`). LGPL 2.1 dynamic-link path is the cleanest license-compliance route ([RD-23](roadmap.md#1-decision-quick-reference)). |
| PD-36 | OpcodeList scope in P1                    | Parse **OpcodeList1 + LinearizationTable + OpcodeList2** *metadata* only; **execution** of OpcodeList2 (GainMap / vignette) and any of OpcodeList3 slips to P2.    |
| PD-37 | CFA pattern coverage                      | All four 2×2 Bayer arrangements: **RGGB / BGGR / GRBG / GBRG**. **Quad Bayer (4×4)** DNGs are detected via `CFARepeatPatternDim == {4,4}` and rejected with a clear error ([RD-9](roadmap.md#1-decision-quick-reference) defers them to P2). |
| PD-38 | DNG output bit-depth                      | LibRaw produces RAW16 (`R16_UINT`) regardless of source bit-depth; 10 / 12 / 14-bit sensors are left-padded by LibRaw to 16-bit. Downstream nodes consume `R16_UINT`. |
| PD-39 | JPEG XL DNG handling                      | P1 **rejects** DNG 1.7+ JXL payloads (versions ≤ 1.6 supported only). Re-evaluated post-P1 once LibRaw + libjxl integration is exercised.                          |
| PD-40 | ActiveArea crop                           | `DngReader` crops to `ActiveArea` + `DefaultCropOrigin` / `DefaultCropSize` at ingest. Downstream nodes consume the cropped Bayer mosaic.                          |
| PD-41 | `DngMetadata` struct                      | Full P1 field set: CFAPattern, CFARepeatPatternDim, BlackLevel (+DeltaH/V), WhiteLevel, LinearizationTable, AsShotNeutral, ColorMatrix1/2, ForwardMatrix1/2, CalibrationIlluminant1/2, ActiveArea, Orientation, Make/Model, ISO, ExposureTime. OpcodeList2 metadata (GainMap entries) is parsed but not executed in P1. |
| PD-42 | 5-node + output order                     | `linearize → blacklevel → wb.dual_illuminant (Bayer domain) → demosaic.bilinear → colormatrix.dng_to_working → heif_sdr`. WB is applied in Bayer domain to preserve highlights, per [Research 12 §1](research/12-dng-format.md). |
| PD-43 | Demosaic algorithm in P1                  | **Bilinear only**. RCD/AMaZE slip to P2 ([RD-5](roadmap.md#1-decision-quick-reference) keeps the 18-node target intact at v1.0).                                  |
| PD-44 | White-balance implementation              | **Full dual-illuminant**: CCT-interpolate ColorMatrix1/2 + ForwardMatrix1/2 by `AsShotNeutral` against CalibrationIlluminant1/2, then derive per-channel Bayer gain. |
| PD-45 | Camera→working color-space transform      | Use **ForwardMatrix1/2** interpolation → XYZ D50 → Bradford CAT → XYZ D65 → linear Rec.2020 D65. `ColorMatrix` inverse path is *not* a fallback in P1 (Pixel 8 Pro DNGs always supply ForwardMatrix). |
| PD-46 | Mid-pipeline buffer precision             | **FP16** working space (`R16G16B16A16_SFLOAT` after demosaic; `R16_SFLOAT` single-channel in Bayer domain per [PD-83](#4-phase-decisions-pd-n)).                                  |
| PD-47 | OCIO + lcms2 integration depth            | OCIO 2.4 + lcms2 2.16 are linked in. The `colormatrix.dng_to_working` node implements the camera→Rec.2020 transform by hand (3×3 multiply + Bradford CAT). OCIO is used at the **output** node only (linear Rec.2020 → sRGB / BT.709 display transform) and to produce the embedded ICC. OCIO Looks menu defers to P3. |
| PD-48 | Embedded OCIO config                      | A minimal `share/cpipe/ocio/config.ocio` with two color spaces (`linear-rec2020-d65`, `srgb-display`) and one display view. Replaced by OCIO built-in studio-config in P2. |
| PD-49 | SDR CICP signalling                       | **CICP `1 / 1 / 1`** (BT.709 primaries, BT.709 transfer, BT.709 matrix). Maximum SDR viewer compatibility.                                                          |
| PD-50 | HEIF bit-depth + chroma                   | **8-bit HEVC Main, yuv444p**. Main10 reserved for P2 HDR. yuv444p avoids 4:2:0 chroma subsampling on photographic content.                                          |
| PD-51 | libheif linkage                           | **Dynamic** via vcpkg (LGPL with static-link clause honoured by dynamic linking, matching LibRaw policy [PD-35](#4-phase-decisions-pd-n)).                                          |
| PD-52 | kvazaar plugin                            | Static-linked as the libheif HEVC encoder plugin. The libheif vcpkg port is configured with `kvazaar=ON` and `x265=OFF` ([RD-23](roadmap.md#1-decision-quick-reference) license discipline). |
| PD-53 | ICC profile generation                    | `lcms2 cmsCreate_sRGBProfile` produces a fresh sRGB v4 ICC at encode time; embedded in HEIF alongside CICP nclx (`1/1/1`) per the [`tech.md` §7](tech.md#7-layer-6--color--format) dual-write rule. |
| PD-54 | Tracy instrumentation                     | Tracy 0.11+ vcpkg dependency added; `CPIPE_ENABLE_TRACY=OFF` by default. Zones wrap `Pipeline::run`, `Scheduler::dispatch_node`, `ComputeContext::submit_halide`, and `BufferAllocator::create` ([`architecture.md` §12 Tracing](architecture.md#12-cross-cutting-concerns)). |
| PD-55 | Scheduler parallelism                     | TaskFlow `Executor` performs **real parallel topological dispatch** with per-instance serialization. Replaces the P0 serial walk (P0 [PD-20](phase-00-foundation.md#4-phase-decisions-pd-n) is now obsolete; P1 ships the upgrade). |
| PD-56 | Memory planner                            | Full **interference-graph coloring** ([Research 03 §5](research/03-heterogeneous-scheduler.md)) + peak-byte pre-check at load. `Pipeline::load` refuses to schedule when `peak > device cap`. |
| PD-57 | Vulkan device plane minimum               | **The `linearize` node** ships a Halide Vulkan AOT variant; the other four ISP nodes plus `heif_sdr` are CPU Halide / CPU code in P1. Validates the GPU plumbing without forcing demosaic onto an immature path. |
| PD-58 | `IBuffer` subclasses in P1                | `CpuBuffer` (from P0) + **`VulkanBuffer`** (storage-buffer kind, host-import path via VMA). `VulkanImage` (sampled images) defers to P2 when sampler-based demosaic lands. |
| PD-59 | Precision-planner stub                    | **Strict consistency check**: at load, every edge's source `precision` must match its sink port's allowed `precision` list. Mismatch fails the load with a clear error. *No* automatic `precision_convert` node insertion in P1 (P3+ work). |
| PD-60 | Synchronization                           | `IFence` (VkFence), binary `VkSemaphore` (acquire-fence import), and timeline `VkSemaphore` are all implemented and used: `linearize` signals timeline value 1; `blacklevel` waits before its CPU read. |
| PD-61 | CLI surface                               | Unchanged from P0: only `cpipe run <input.dng> -p <pipe.json> -o <out.heif>`. `info / serve / bench / iqa / model` defer to later phases.                          |
| PD-62 | Manifest schema bump                      | New `schemas/node-v0.2.json` + `schemas/pipeline-v0.2.json` with `ports[].caps`, `ports[].precision`, `compute.precision`, and `color.input_role / output_role` fields. Top-level `inputs[]` block added to pipeline schema for [PD-67](#4-phase-decisions-pd-n). |
| PD-63 | Golden reference + PSNR threshold         | Reference output is regenerated from a pinned RawTherapee CLI (`rawtherapee-cli -d -j -Y` with `bilinear` demosaic and matching color-matrix path); pinned RT version + flags recorded in `scripts/generate_golden.sh`. Per-node golden tolerance **PSNR ≥ 40 dB**; end-to-end DNG→HEIF tolerance **PSNR ≥ 35 dB** (HEVC quantization adds 4–5 dB loss vs the EXR reference). The roadmap §4 DoD "PSNR within 3 dB" target is met against the *same* bilinear reference. |
| PD-64 | Golden fixture granularity                | Per-node: `tests/golden/<node-id>/in.exr` + `out.exr`. Plus a single end-to-end pair `tests/golden/e2e_dng_to_heif/{in.dng, out.heif}`.                            |
| PD-65 | Git LFS bootstrap                         | `.gitattributes` adds `*.exr filter=lfs diff=lfs merge=lfs -text` and `*.dng filter=lfs ...`. CI installs `git-lfs` and runs `git lfs pull` before `cmake`.       |
| PD-66 | Test corpus source                        | Pixel 8 Pro DNG samples sourced from [raw.pixls.us](https://raw.pixls.us/) (CC-BY) and committed under `tests/corpus/`. Attribution recorded in `tests/corpus/README.md`. |
| PD-67 | Pipeline JSON inputs                      | Pipeline schema gains a top-level `inputs: [{ "port": "raw", "source": "dng" }]` block. Edges use `$inputs.<port>` to address external producers. `architecture.md` §11 example is updated accordingly ([PD-82](#4-phase-decisions-pd-n)). |
| PD-68 | OpenImageIO usage                         | `OpenImageIO 2.5` (vcpkg) reads / writes `.exr` golden fixtures from the test harness. Not linked into `cpipe-runtime`; only `tests/golden/`.                      |
| PD-69 | IQA / benchmark harness                   | **Deferred to P3** per [RD-13 / RD-15](roadmap.md#1-decision-quick-reference). P1 implements a ~50-line C++ PSNR helper in `tests/golden/psnr.cpp`. No piq / pyiqa / Google Benchmark / nanobench dependencies in P1. |
| PD-70 | CI Vulkan validation                      | Workflow `build-and-test.yml` installs Mesa **Lavapipe** on `ubuntu-24.04` for the Vulkan path. `apt install mesa-vulkan-drivers libvulkan1`. `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`. |
| PD-71 | vcpkg manifest growth cadence             | Dependencies added **per T-task** (LibRaw in T3, OCIO/lcms2/libheif/kvazaar/OpenImageIO in T10, VMA/Vulkan-Headers/Vulkan-Loader in T6, Tracy in T11) to keep PR diffs reviewable. T2 sets up `vcpkg-configuration.json` overlay scaffold. |
| PD-72 | Halide Vulkan target                      | CMake option `CPIPE_ENABLE_HALIDE_VULKAN=ON` (default) forces Halide `FetchContent` to rebuild with the Vulkan backend. Cold build budget revised to **40–50 min** with vcpkg binary cache (PD-13 is updated for P1). Hot build stays ≤ 5 min. |
| PD-73 | ASAN + Vulkan compatibility               | Debug preset adds `LSAN_OPTIONS=suppressions=$(pwd)/tests/lsan_suppressions.txt`. The suppression file silences known Mesa / NVIDIA ICD leaks; first-party leaks remain reported.                                                  |
| PD-74 | ABI version                               | `CPIPE_ABI_MAJOR=0`, `CPIPE_ABI_MINOR=1` **unchanged**. No new suite is introduced in P1; the schema bump is independent of the ABI per [`plugin-sdk.md` §4.3](plugin-sdk.md#43-per-suite-versioning).                  |
| PD-75 | Open-Question discipline                  | P1 touches **no** open question. The phase doc records that Q15 (editor-side authoring, resolved "no") stays consistent — Pipeline JSON `inputs` is consumed by the Editor in P3 without re-opening Q15. |
| PD-76 | 24 h CI green gate                        | Per P0 [PD-31](phase-00-foundation.md#4-phase-decisions-pd-n) precedent the gate is **default 24 h**; the maintainer may override with documented rationale recorded in this phase doc's "What Shipped / What Slipped" section. |
| PD-77 | CHANGELOG.md                              | **Not created in P1** (PD-2 carried). `phase-XX-*.md` + `roadmap.md` + `git log` remain the changelog surface.                                                                                          |
| PD-78 | P0 closure carry-forward                  | This phase doc records P0 closure state inherited from `phase-00-foundation.md` PD-31. T1 does *not* re-verify the latest five P0 main runs; it inherits P0's tag.                                                                |
| PD-79 | Cold-build / hot-build budget             | P1 revises P0 [PD-13](phase-00-foundation.md#4-phase-decisions-pd-n): **cold ≤ 50 min**, **hot ≤ 5 min** (vcpkg binary cache + ccache). T2 verifies the bound on a fresh CI run.                                                  |
| PD-80 | `heif_sdr` node output kind               | `heif_sdr` produces a `BufferKind::Blob` (encoded HEVC bytestream). `Pipeline::run` returns it inside `RunOutputs["heif"]`. The CLI extracts the Blob and writes it to `-o path`. No filesystem I/O inside the node. |
| PD-81 | Debug intermediate EXR dump               | **Not in P1**. Intermediate `EXR` dumping (a `com.cpipe.debug.dump_exr` node) defers to P2 or P3. P1 nodes are observed through Tracy zones and golden fixtures only. |
| PD-82 | `architecture.md` §11 + §17 patch         | §11 example replaces the imaginary `com.cpipe.builtin.dng_input` node with the canonical top-level `inputs` block + `$inputs.raw` edges. §17 Q15 row adds a footnote that v1's external-input mechanism does not require editor-side node authoring. |
| PD-83 | `buffer.md` §3 PixelFormat extension      | Add `R16_SFLOAT` (single-channel FP16, 2 B/pixel) before `BLOB`. Used by Bayer-domain intermediates (`linearize` / `blacklevel` / `wb.dual_illuminant` outputs). The enum table grows from 14 → 15 entries; P0 test `test_pixel_format` is extended accordingly.                       |

---

## 5. Repository Layout (P1 deltas vs P0)

P0's layout is preserved; the following new files and directories appear during P1. Empty CMakeLists.txt placeholders from P0 fill in actual sources.

```
cpipe/
├── .gitattributes                       # +*.exr / *.dng LFS rules (T1)
├── .github/workflows/
│   └── build-and-test.yml               # +mesa-vulkan-drivers install (T2)
├── cmake/
│   ├── CompilerOptions.cmake            # +LSAN_OPTIONS suppression (T2)
│   ├── HalideHelpers.cmake              # +Vulkan target arm (T2)
│   └── FindVulkanIcds.cmake             # +Lavapipe selection helper (T6)
├── include/cpipe/
│   ├── core/
│   │   ├── PixelFormat.hpp              # +R16_SFLOAT (T1, PD-83)
│   │   └── VulkanBuffer.hpp             # (T6)
│   ├── ingest/
│   │   ├── DngReader.hpp                # (T3)
│   │   ├── DngMetadata.hpp              # (T3)
│   │   └── dng_opcode.hpp               # (T3)
│   ├── runtime/
│   │   ├── VulkanContext.hpp            # (T6)
│   │   ├── HalideVulkanAdapter.hpp      # (T6)
│   │   ├── MemoryPlanner.hpp            # (T11)
│   │   └── Sync.hpp                     # IFence/ITimeline (T6)
│   └── color/
│       ├── BradfordCat.hpp              # (T9)
│       ├── CctInterp.hpp                # (T7)
│       ├── IccGenerator.hpp             # (T10)
│       ├── OcioContext.hpp              # (T10)
│       └── HeifWriter.hpp               # (T10)
├── share/cpipe/
│   └── ocio/
│       └── config.ocio                  # minimal 2-space study config (T10, PD-48)
├── schemas/
│   ├── node-v0.2.json                   # (T1, PD-62)
│   └── pipeline-v0.2.json               # (T1, PD-62)
├── src/cpipe/
│   ├── core/
│   │   ├── PixelFormat.cpp              # +R16_SFLOAT to_string (T1)
│   │   └── VulkanBuffer.cpp             # (T6)
│   ├── ingest/
│   │   ├── DngReader.cpp                # LibRaw 0.22 dynamic-link wrapper (T3)
│   │   └── dng_opcode.cpp               # OpcodeList1/2 metadata parser (T3)
│   ├── runtime/
│   │   ├── Scheduler.cpp                # parallel topo dispatch upgrade (T11)
│   │   ├── MemoryPlanner.cpp            # interference-graph coloring (T11)
│   │   ├── VulkanContext.cpp            # VMA init + queue (T6)
│   │   ├── HalideVulkanAdapter.cpp      # cpipe_buffer_t → halide_buffer_t (T6)
│   │   ├── Sync.cpp                     # VkFence + VkSemaphore wrappers (T6)
│   │   └── TracyShim.hpp                # CPIPE_ZONE / CPIPE_ZONE_N (T11)
│   ├── nodes/
│   │   ├── linearize/
│   │   │   ├── linearize.cpp + .json + linearize_generator.cpp   # (T5/T6)
│   │   ├── blacklevel/
│   │   │   ├── blacklevel.cpp + .json + blacklevel_generator.cpp # (T7)
│   │   ├── wb_dual_illuminant/
│   │   │   ├── wb_dual_illuminant.cpp + .json + wb_generator.cpp # (T7)
│   │   ├── demosaic_bilinear/
│   │   │   ├── demosaic_bilinear.cpp + .json + bilinear_generator.cpp # (T8)
│   │   ├── colormatrix_dng_to_working/
│   │   │   ├── colormatrix.cpp + .json + colormatrix_generator.cpp    # (T9)
│   │   └── output_heif_sdr/
│   │       ├── output_heif_sdr.cpp + .json   # (T10)
│   └── color/
│       ├── BradfordCat.cpp                  # (T9)
│       ├── CctInterp.cpp                    # (T7)
│       ├── IccGenerator.cpp                 # lcms2 sRGB profile (T10)
│       ├── OcioContext.cpp                  # config.ocio loader (T10)
│       └── HeifWriter.cpp                   # libheif/kvazaar facade (T10)
├── tests/
│   ├── corpus/
│   │   ├── pixel8pro_d65.dng            # raw.pixls.us CC-BY (T3, LFS)
│   │   └── README.md                    # attribution per fixture
│   ├── golden/
│   │   ├── linearize/{in,out}.exr       # (T12)
│   │   ├── blacklevel/{in,out}.exr      # (T12)
│   │   ├── wb_dual_illuminant/{in,out}.exr   # (T12)
│   │   ├── demosaic_bilinear/{in,out}.exr    # (T12)
│   │   ├── colormatrix/{in,out}.exr     # (T12)
│   │   ├── e2e_dng_to_heif/{in.dng,out.heif} # (T12)
│   │   ├── psnr.cpp                     # ~50-line manual PSNR helper (T12)
│   │   └── run_golden.cpp               # Catch2 driver (T12)
│   ├── unit/
│   │   ├── test_pixel_format.cpp        # extended for R16_SFLOAT (T1)
│   │   ├── test_dng_reader.cpp          # (T3)
│   │   ├── test_dng_opcode_meta.cpp     # (T3)
│   │   ├── test_precision_planner.cpp   # (T4)
│   │   ├── test_linearize_node.cpp      # (T5)
│   │   ├── test_vulkan_buffer.cpp       # (T6)
│   │   ├── test_halide_vulkan.cpp       # (T6)
│   │   ├── test_timeline_semaphore.cpp  # (T6)
│   │   ├── test_blacklevel_node.cpp     # (T7)
│   │   ├── test_wb_dual_illuminant.cpp  # covers cct_interp (T7)
│   │   ├── test_demosaic_bilinear.cpp   # (T8)
│   │   ├── test_colormatrix.cpp         # covers bradford_cat (T9)
│   │   ├── test_heif_writer.cpp         # (T10)
│   │   ├── test_ocio_lcms2.cpp          # OCIO transform + ICC roundtrip (T10)
│   │   ├── test_taskflow_parallel.cpp   # (T11)
│   │   ├── test_interference_graph.cpp  # (T11)
│   │   └── test_passthrough_node.cpp    # P0 — unchanged
│   ├── integration/
│   │   ├── test_passthrough_end_to_end.cpp     # P0 — unchanged
│   │   └── test_dng_to_heif_e2e.cpp            # (T12)
│   ├── fixtures/
│   │   ├── min-pipeline.cpipe.json      # 5-node + heif_sdr graph (T12)
│   │   └── invalid-pipeline.cpipe.json  # P0 — unchanged
│   └── lsan_suppressions.txt            # mesa3d / libVkICD_* leaks (T2)
└── vcpkg.json                           # +9 deps over P1 lifetime (T2 / T3 / T6 / T10 / T11)
```

The six P0 CMake targets stay; new code lands inside `cpipe-runtime`, `cpipe-builtin-nodes`, and a freshly-populated `cpipe::ingest` namespace that lives inside `cpipe-runtime` (per [`buffer.md` §10.1](buffer.md#101-dngreader)).

---

## 6. Task List

Twelve vertical tasks ([PD-34](#4-phase-decisions-pd-n)). Each ships in dependency order so the repo never enters a half-built state. Sizes per the [task sizing guide](https://github.com/addy-ai/agent-skills/blob/main/skills/planning-and-task-breakdown/SKILL.md): S = 1–2 files; M = 3–5; L = 5–8.

### T1 — P1 Bootstrap + Schema v0.2 + Cross-Doc Patches

**Description.** Write `phase-01-walking-skeleton.md`; update `roadmap.md` §2.1 (P1 *Planned* → *In Progress*) and `README.md` Current Status; patch `architecture.md` §11 example + §17 Q15 footnote per [PD-82](#4-phase-decisions-pd-n); patch `buffer.md` §3 enum table per [PD-83](#4-phase-decisions-pd-n); land `schemas/node-v0.2.json` + `schemas/pipeline-v0.2.json` ([PD-62](#4-phase-decisions-pd-n)); add `.gitattributes` LFS rules ([PD-65](#4-phase-decisions-pd-n)). Extend P0 `test_pixel_format` to cover `R16_SFLOAT`.

**Acceptance criteria:**
- [x] `docs/phase-01-walking-skeleton.md` is committed and renders on GitHub.
- [x] `docs/roadmap.md` §2.1 P1 row reads *In Progress* with the new evidence link.
- [x] `README.md` Current Status table P1 row reads *In Progress*.
- [x] `docs/architecture.md` §11 example no longer references `com.cpipe.builtin.dng_input`; §17 Q15 row carries the v1-input footnote.
- [x] `docs/buffer.md` §3 PixelFormat enum table includes `R16_SFLOAT` with byte-width 2 and a one-line rationale.
- [x] `schemas/node-v0.2.json` + `schemas/pipeline-v0.2.json` validate the P0 passthrough manifest and the (placeholder) 5-node + `heif_sdr` graph.
- [x] `.gitattributes` declares `*.exr` and `*.dng` LFS-tracked; `git lfs ls-files` is empty (no binaries yet).
- [x] `ctest -R test_pixel_format` green with the new `R16_SFLOAT` case.

**Verification:**
- [x] `pre-commit run --all-files` passes (including the existing Ajv manifest hook against the new schemas).
- [x] `gh pr view --json files` shows the touched docs and schemas only — no source changes outside `include/cpipe/core/PixelFormat.hpp`.

**Dependencies:** None.

**Files likely touched:**
- `docs/phase-01-walking-skeleton.md` (new), `docs/roadmap.md`, `docs/architecture.md`, `docs/buffer.md`, `README.md`
- `schemas/node-v0.2.json`, `schemas/pipeline-v0.2.json`
- `.gitattributes`
- `include/cpipe/core/PixelFormat.hpp`, `src/cpipe/core/PixelFormat.cpp`, `tests/unit/test_pixel_format.cpp`

**Estimated scope:** M (5 docs + 2 schemas + 3 code; primarily prose).

---

### T2 — Build & CI Deltas: Halide Vulkan, Lavapipe, vcpkg Scaffolding, ASAN Suppression

**Description.** Switch on `CPIPE_ENABLE_HALIDE_VULKAN=ON` ([PD-72](#4-phase-decisions-pd-n)); install Mesa Lavapipe in `build-and-test.yml` and wire `VK_ICD_FILENAMES` for the test step ([PD-70](#4-phase-decisions-pd-n)); add `LSAN_OPTIONS=suppressions=...` to the Debug preset ([PD-73](#4-phase-decisions-pd-n)); land the `vcpkg-configuration.json` overlay scaffold so subsequent T-tasks can drop in libraw / libheif / kvazaar / OCIO / lcms2 / OpenImageIO / VMA / Vulkan-Headers / Vulkan-Loader / Tracy without churn. No new dependency is *enabled* yet — only the structural deltas.

**Acceptance criteria:**
- [ ] `cmake --preset linux-debug` reconfigures with `CPIPE_ENABLE_HALIDE_VULKAN=ON` and Halide rebuilds with the Vulkan backend.
- [ ] CI `build-debug` job installs `mesa-vulkan-drivers libvulkan1 git-lfs` and exports `VK_ICD_FILENAMES` for the test step; a dummy Vulkan smoke test (`vulkaninfo --summary`) succeeds.
- [ ] `tests/lsan_suppressions.txt` is wired into both Debug presets; ASAN+UBSAN P0 tests still pass.
- [ ] `vcpkg-configuration.json` carries an empty `overlay-ports` block ready for downstream T to populate; the cold rebuild budget remains ≤ 50 min on first CI run after this T.
- [ ] No regression in P0 tests; existing 25 tests still green.

**Verification:**
- [ ] CI run logs show `Halide Vulkan backend: ENABLED` and `Lavapipe detected (VK_ICD_FILENAMES=...)`.
- [ ] `vulkaninfo --summary` in the CI test step prints `GPU0 = llvmpipe`.

**Dependencies:** T1.

**Files likely touched:**
- `CMakePresets.json`, `cmake/CompilerOptions.cmake`, `cmake/HalideHelpers.cmake`, `cmake/FindVulkanIcds.cmake`
- `.github/workflows/build-and-test.yml`
- `tests/lsan_suppressions.txt`
- `vcpkg.json`, `vcpkg-configuration.json`

**Estimated scope:** M (6 build files + 1 CI file + 1 suppression file).

---

### T3 — DngReader (Host Class) + LibRaw + OpcodeList Metadata Parser + DngMetadata

**Description.** Add `libraw` to `vcpkg.json` (dynamic link, [PD-35](#4-phase-decisions-pd-n)). Implement `cpipe::ingest::DngReader` per [`buffer.md` §10.1](buffer.md#101-dngreader): reads a DNG via LibRaw 0.22, crops to ActiveArea ([PD-40](#4-phase-decisions-pd-n)), wraps the RAW16 buffer via `BufferAllocator::import_host_ptr`, returns `{ std::shared_ptr<IBuffer> raw; DngMetadata meta; }`. Implement `cpipe::ingest::dng_opcode` as a metadata-only parser ([PD-36](#4-phase-decisions-pd-n)) — it reads OpcodeList1 / LinearizationTable / OpcodeList2 entries into a `std::vector<DngOpcode>` for later phases but does *not* execute them. `DngMetadata` carries the full P1 field set ([PD-41](#4-phase-decisions-pd-n)). Commit `tests/corpus/pixel8pro_d65.dng` via LFS ([PD-66](#4-phase-decisions-pd-n)).

**Acceptance criteria:**
- [ ] `DngReader::read("tests/corpus/pixel8pro_d65.dng")` succeeds; returned `raw->layout()` is `{ kind=Image2D, format=R16_UINT, dims={crop_w, crop_h} }`.
- [ ] `DngMetadata` populated fields match `exiftool` reference output for the corpus DNG within ULP for floats.
- [ ] All 4 CFA patterns (RGGB / BGGR / GRBG / GBRG) deserialize correctly; a Quad Bayer (`CFARepeatPatternDim == {4,4}`) DNG triggers a clear error per [PD-37](#4-phase-decisions-pd-n).
- [ ] OpcodeList2 metadata extraction recognizes at minimum `GainMap` entries; no execution attempted.
- [ ] JPEG XL payload DNGs return `CPIPE_UNSUPPORTED` per [PD-39](#4-phase-decisions-pd-n).
- [ ] `libraw` is linked dynamically (`ldd build/.../cpipe | grep libraw` shows the .so).

**Verification:**
- [ ] `ctest -R test_dng_reader` green.
- [ ] `ctest -R test_dng_opcode_meta` green (synthetic OpcodeList2 byte stream → parsed entries).

**Dependencies:** T1, T2.

**Files likely touched:**
- `include/cpipe/ingest/{DngReader,DngMetadata,dng_opcode}.hpp`
- `src/cpipe/ingest/{DngReader,dng_opcode}.cpp`
- `tests/unit/test_dng_reader.cpp`, `tests/unit/test_dng_opcode_meta.cpp`
- `tests/corpus/pixel8pro_d65.dng` (LFS), `tests/corpus/README.md`
- `vcpkg.json` (+libraw)

**Estimated scope:** L (3 headers + 2 implementation + 2 tests + corpus + vcpkg).

---

### T4 — Working-Space Types + Precision-Planner Stub

**Description.** With `R16_SFLOAT` already in the enum ([PD-83](#4-phase-decisions-pd-n) / T1), make `CpuBuffer` allocate correctly for the new format; add `R16G16B16A16_SFLOAT` allocator coverage tests; implement the precision-planner stub in `Pipeline::load` per [PD-59](#4-phase-decisions-pd-n): for every edge, validate that source-port `precision` is in sink-port `precision` list; mismatch fails the load with the offending edge name.

**Acceptance criteria:**
- [ ] `CpuBuffer` constructs cleanly for `R16_SFLOAT` and `R16G16B16A16_SFLOAT` with correct `size_bytes()` and stride alignment.
- [ ] `Pipeline::load` of a 3-node mock graph with matching precisions succeeds.
- [ ] `Pipeline::load` of a 3-node mock graph with one mismatched edge fails with a clear error message that names both ports.
- [ ] No `precision_convert` node is inserted (stub-only).

**Verification:**
- [ ] `ctest -R test_precision_planner` green.
- [ ] `ctest -R test_cpu_buffer` (P0) still green; new format coverage added.

**Dependencies:** T1.

**Files likely touched:**
- `src/cpipe/core/CpuBuffer.cpp` (extend for R16_SFLOAT)
- `src/cpipe/runtime/Pipeline.cpp` (precision-planner stub)
- `tests/unit/test_cpu_buffer.cpp` (P0 — extend), `tests/unit/test_precision_planner.cpp` (new)

**Estimated scope:** S (2 source + 2 tests).

---

### T5 — `linearize` Node (CPU Halide)

**Description.** Author the Halide generator `linearize_generator.cpp` that applies the DNG LinearizationTable as a 1-D LUT over `R16_UINT` input and emits `R16_SFLOAT` output normalized to `[0, 1]`. Write `nodes/linearize/linearize.cpp` (the C++ `Linearize` class implementing `sdk::Node`) and `linearize.json` manifest declaring `compute.engine="Halide"`, `compute.device="CPU"`, `compute.halide_aot=["linearize_cpu"]`, `ports[].precision=["r16_uint","r16_sfloat"]`, and `compute.in_pixel_bytes / out_pixel_bytes` per [Plugin SDK §7](plugin-sdk.md#7-manifest-schema). Register via `CPIPE_REGISTER_NODE`.

**Acceptance criteria:**
- [ ] Halide AOT `linearize_cpu.{o,a}` is generated by `add_halide_library`.
- [ ] `nodes/linearize/linearize.json` validates against `schemas/node-v0.2.json` (pre-commit Ajv hook).
- [ ] `Linearize::process()` on a synthetic 64×64 Bayer R16_UINT input produces R16_SFLOAT output matching the reference LinearizationTable to within 1 ULP.
- [ ] Descriptor `com.cpipe.builtin.linearize` appears in the registry at startup.

**Verification:**
- [ ] `ctest -R test_linearize_node` green.

**Dependencies:** T3, T4.

**Files likely touched:**
- `src/cpipe/nodes/linearize/{linearize.cpp,linearize.json,linearize_generator.cpp}`
- `tests/unit/test_linearize_node.cpp`

**Estimated scope:** M (3 source + 1 manifest + 1 test).

---

### T6 — Vulkan Device Plane + VulkanBuffer + Halide Vulkan + Sync + `linearize` Vulkan Variant

**Description.** Stand up the Vulkan device plane minimum. Add VMA / Vulkan-Headers / Vulkan-Loader to `vcpkg.json`. Implement `runtime/VulkanContext` (instance, physical-device selection preferring discrete GPU, logical device with timeline-semaphore feature, single queue, VMA allocator init). Implement `core/VulkanBuffer` (VMA-backed `VkBuffer`, storage-buffer usage, host-import `import_host_ptr` path per [`buffer.md` §7.2](buffer.md#72-external-imports)). Implement `runtime/HalideVulkanAdapter` that adapts `cpipe_buffer_t*` → `halide_buffer_t*` with the Vulkan device interface. Implement `runtime/Sync.hpp` IFence / ITimeline backed by `VkFence` and timeline `VkSemaphore` per [`buffer.md` §8](buffer.md#8-synchronization-host-only). Extend the `linearize` Halide library with a `host-vulkan` target ([PD-57](#4-phase-decisions-pd-n)). Add `CpuBuffer ↔ VulkanBuffer` handoff (`vkCmdCopyBuffer` from a temporary host-visible staging buffer or zero-copy via `VK_EXT_external_memory_host` where 4 KB-aligned). Linearize emits timeline value 1; blacklevel (CPU, T7) waits before its first read ([PD-60](#4-phase-decisions-pd-n)).

**Acceptance criteria:**
- [ ] `VulkanContext` initializes on Lavapipe and NVIDIA RTX (developer machine).
- [ ] `VulkanBuffer` allocated through VMA round-trips a host pointer with byte-identical contents.
- [ ] `HalideVulkanAdapter` invokes `linearize_vulkan` on a `VulkanBuffer` input, producing output equal to the CPU variant within 1 ULP at FP16.
- [ ] Timeline semaphore advances from 0 → 1 after `linearize` completes; a host wait succeeds.
- [ ] CI Lavapipe run executes the Vulkan path; PSNR vs golden ≥ 50 dB.

**Verification:**
- [ ] `ctest -R test_vulkan_buffer` green.
- [ ] `ctest -R test_halide_vulkan` green.
- [ ] `ctest -R test_timeline_semaphore` green.

**Dependencies:** T5.

**Files likely touched:**
- `include/cpipe/core/VulkanBuffer.hpp`, `include/cpipe/runtime/{VulkanContext,HalideVulkanAdapter,Sync}.hpp`
- `src/cpipe/core/VulkanBuffer.cpp`, `src/cpipe/runtime/{VulkanContext,HalideVulkanAdapter,Sync}.cpp`
- `src/cpipe/nodes/linearize/linearize_generator.cpp` (+vulkan target)
- `cmake/HalideHelpers.cmake` (vulkan target wiring)
- `tests/unit/{test_vulkan_buffer,test_halide_vulkan,test_timeline_semaphore}.cpp`
- `vcpkg.json` (+vulkan-memory-allocator +vulkan-headers +vulkan-loader)

**Estimated scope:** L (8 source + 3 tests + vcpkg/cmake). Maintainer should consider further sub-splitting T6 if the diff exceeds ~500 lines.

---

### Checkpoint A — after T1–T4

- [ ] T1–T4 merged; `main` is green on Lavapipe CI.
- [ ] Schema v0.2 in place; precision-planner stub gates pipeline loads.
- [ ] `DngReader` reads the Pixel 8 Pro corpus DNG; `DngMetadata` populated.
- [ ] Review: any unexpected vcpkg port pulls? Any binary-cache misses on first CI run?

### Checkpoint B — after T5–T9 (also covers Vulkan landing in T6)

- [ ] All five ISP nodes ship; each has a unit test and a golden EXR pair.
- [ ] `linearize` runs on Vulkan (Lavapipe in CI; NVIDIA locally); the other four nodes run on CPU Halide AOT.
- [ ] CPU↔GPU handoff via timeline semaphore validated on Lavapipe.
- [ ] Review: peak VRAM on the corpus DNG within the per-device budget computed by [PD-56](#4-phase-decisions-pd-n)?

### Checkpoint C — after T10–T12 (= P1 DoD)

- [ ] DoD verification commands in [§10](#10-definition-of-done-verification-commands) all pass.
- [ ] CI has been green for ≥ 24 h on `main` (or the gate is overridden per [PD-76](#4-phase-decisions-pd-n) with maintainer rationale recorded in "What Shipped / What Slipped").
- [ ] Tag `v0.2` is live.

---

### T7 — `blacklevel` + `wb.dual_illuminant` Nodes + CCT-Interpolation Module

**Description.** Author two CPU Halide nodes: `blacklevel` subtracts the per-channel `BlackLevel` (+ DeltaH/V) and rescales against `WhiteLevel - BlackLevel` to map RAW16 into normalized `R16_SFLOAT`. `wb.dual_illuminant` applies per-channel Bayer-domain gain derived from a CCT-interpolated mix of `ColorMatrix1/2` and `ForwardMatrix1/2` against `AsShotNeutral` ([PD-44](#4-phase-decisions-pd-n)). The CCT interpolation lives in `cpipe::color::CctInterp` (shared with T9's Bradford CAT logic).

**Acceptance criteria:**
- [ ] Both manifests validate against `node-v0.2.json`; both registered.
- [ ] On a synthetic 64×64 input with known BlackLevel/WhiteLevel, `blacklevel.process()` produces FP16 output matching the analytical formula to within 1 ULP.
- [ ] `wb.dual_illuminant.process()` on a synthetic CCT=4500K input mixes `ForwardMatrix1` (StdA, 2856K) and `ForwardMatrix2` (D65, 6504K) by `1/CCT` linear interpolation per the DNG spec; resulting channel gains match a hand-computed reference within 1e-4.
- [ ] `CctInterp` unit tests cover edge cases (single-illuminant DNG → no interpolation; CCT outside [2856, 6504] → clamp).

**Verification:**
- [ ] `ctest -R test_blacklevel_node` green.
- [ ] `ctest -R test_wb_dual_illuminant` green (covers `CctInterp`).

**Dependencies:** T5 (linearize establishes the FP16 Bayer-domain pipeline).

**Files likely touched:**
- `include/cpipe/color/CctInterp.hpp`, `src/cpipe/color/CctInterp.cpp`
- `src/cpipe/nodes/blacklevel/{blacklevel.cpp,blacklevel.json,blacklevel_generator.cpp}`
- `src/cpipe/nodes/wb_dual_illuminant/{wb_dual_illuminant.cpp,wb_dual_illuminant.json,wb_generator.cpp}`
- `tests/unit/{test_blacklevel_node,test_wb_dual_illuminant}.cpp`

**Estimated scope:** L (7 source + 1 shared + 2 tests).

---

### T8 — `demosaic.bilinear` Node

**Description.** Author a Halide-generated bilinear demosaic for the four 2×2 CFA patterns ([PD-37](#4-phase-decisions-pd-n)). Input is a single-channel `R16_SFLOAT` Bayer mosaic with the WB-gain applied; output is a 4-channel `R16G16B16A16_SFLOAT` (alpha = 1.0) image at the same dims. The generator branches on a `cfa_pattern` enum parameter; no run-time dispatch.

**Acceptance criteria:**
- [ ] Manifest validates; descriptor `com.cpipe.demosaic.bilinear` registered.
- [ ] On a 16×16 synthetic RGGB ramp, output PSNR vs the hand-computed bilinear reference ≥ 60 dB.
- [ ] BGGR / GRBG / GBRG inputs produce equivalent quality (golden fixtures per pattern in T12).
- [ ] No artifacts on a Pixel 8 Pro DNG (visual inspection, recorded in PR description).

**Verification:**
- [ ] `ctest -R test_demosaic_bilinear` green.

**Dependencies:** T7.

**Files likely touched:**
- `src/cpipe/nodes/demosaic_bilinear/{demosaic_bilinear.cpp,demosaic_bilinear.json,bilinear_generator.cpp}`
- `tests/unit/test_demosaic_bilinear.cpp`

**Estimated scope:** M (3 source + 1 test).

---

### T9 — `colormatrix.dng_to_working` Node + Bradford CAT

**Description.** Author a Halide-generated 3×3 matrix-multiply node ([PD-45](#4-phase-decisions-pd-n)). Per-instance state caches the composed transform: CCT-interpolated `ForwardMatrix` → XYZ D50 → Bradford CAT D50→D65 → XYZ D65 → linear Rec.2020 D65 primaries. The transform is computed once in `create()` from `DngMetadata` and applied per-pixel in `process()`. `cpipe::color::BradfordCat` implements the standard Bradford chromatic-adaptation matrix.

**Acceptance criteria:**
- [ ] Manifest validates; descriptor `com.cpipe.colormatrix.dng_to_working` registered.
- [ ] Round-trip a known Pixel 8 Pro ColorMatrix2 + ForwardMatrix2 (D65) → resulting working-space pixel matches the analytical reference within 1e-3 (FP16 noise floor).
- [ ] On a `(R=1, G=0, B=0)` synthetic camera-RGB input, the output is within tolerance of the camera-primary chromaticity expressed in Rec.2020 D65 (reference values committed in `tests/unit/test_colormatrix.cpp`).

**Verification:**
- [ ] `ctest -R test_colormatrix` green (covers `BradfordCat`).

**Dependencies:** T7 (shares `CctInterp`).

**Files likely touched:**
- `include/cpipe/color/BradfordCat.hpp`, `src/cpipe/color/BradfordCat.cpp`
- `src/cpipe/nodes/colormatrix_dng_to_working/{colormatrix.cpp,colormatrix.json,colormatrix_generator.cpp}`
- `tests/unit/test_colormatrix.cpp`

**Estimated scope:** M (5 source + 1 test).

---

### T10 — `output.heif_sdr` Node + libheif / kvazaar / lcms2 / OCIO Wiring

**Description.** Add `libheif` (dynamic, [PD-51](#4-phase-decisions-pd-n)), `kvazaar` (HEVC plugin, [PD-52](#4-phase-decisions-pd-n)), `opencolorio`, `lcms`, and `openimageio` to `vcpkg.json` ([PD-71](#4-phase-decisions-pd-n)). Implement `cpipe::color::OcioContext` loading `share/cpipe/ocio/config.ocio` ([PD-48](#4-phase-decisions-pd-n)) with two colour spaces and one display view. Implement `cpipe::color::IccGenerator::sRGBv4()` using `cmsCreate_sRGBProfile` ([PD-53](#4-phase-decisions-pd-n)). Implement `cpipe::color::HeifWriter` that takes RGB FP16 input, applies the OCIO Rec.2020-D65 → sRGB display transform, quantizes to 8-bit yuv444p, hands off to libheif/kvazaar with CICP `1/1/1` ([PD-49](#4-phase-decisions-pd-n)) + ICC dual-write. Author the `output.heif_sdr` node ([PD-80](#4-phase-decisions-pd-n)) which calls `HeifWriter`, allocates a `BufferKind::Blob` output, and returns the encoded bytestream there.

**Acceptance criteria:**
- [ ] Manifest validates; descriptor `com.cpipe.output.heif_sdr` registered. `compute.device="CPU"`, `compute.engine="Halide"` (well — for the OCIO transform; the libheif call is plain C++ inside `process()`).
- [ ] On a synthetic 4×4 linear Rec.2020 D65 input, the produced HEIF, decoded back via `heif-convert`, round-trips to within ΔE2000 ≤ 2 of the input under sRGB display assumption.
- [ ] CICP nclx box reads `1/1/1`; ICC `prof` box is a valid sRGB v4 profile (verified via `iccdump`).
- [ ] HEIF size on a 16 MP test image is < 4 MB.

**Verification:**
- [ ] `ctest -R test_heif_writer` green.
- [ ] `ctest -R test_ocio_lcms2` green.

**Dependencies:** T9.

**Files likely touched:**
- `include/cpipe/color/{OcioContext,IccGenerator,HeifWriter}.hpp`
- `src/cpipe/color/{OcioContext,IccGenerator,HeifWriter}.cpp`
- `src/cpipe/nodes/output_heif_sdr/{output_heif_sdr.cpp,output_heif_sdr.json}`
- `share/cpipe/ocio/config.ocio`
- `tests/unit/{test_heif_writer,test_ocio_lcms2}.cpp`
- `vcpkg.json` (+libheif[kvazaar] +kvazaar +opencolorio +lcms +openimageio)

**Estimated scope:** L (8 source + 1 manifest + 1 ocio + 2 tests + vcpkg).

---

### T11 — Scheduler Upgrade: TaskFlow Parallel Topo + Interference-Graph Memory Planner + Tracy Zones

**Description.** Upgrade `runtime/Scheduler` from P0's serial dispatch ([P0 PD-20](phase-00-foundation.md#4-phase-decisions-pd-n) obsoleted) to TaskFlow-driven parallel topological dispatch ([PD-55](#4-phase-decisions-pd-n)) with per-node-instance serialization. Implement `runtime/MemoryPlanner` with interference-graph coloring ([PD-56](#4-phase-decisions-pd-n), [Research 03 §5](research/03-heterogeneous-scheduler.md)): nodes ↔ live-ranges; chromatic-number = pool slot count; allocate intermediates from a single pool sized by peak. `Pipeline::load` refuses to schedule when `peak > device cap`. Add `runtime/TracyShim.hpp` with `CPIPE_ZONE` / `CPIPE_ZONE_N` macros ([PD-54](#4-phase-decisions-pd-n)); zones land in the four sites listed in §4 PD-54. Add `tracy` to `vcpkg.json` (default feature OFF — only header included when `CPIPE_ENABLE_TRACY=ON`).

**Acceptance criteria:**
- [ ] `Scheduler::dispatch` runs independent nodes concurrently on TaskFlow workers (validated with a synthetic 2-source-1-sink DAG).
- [ ] Same-instance `process()` is serialized across runs (validated with TSan run in a follow-up T-CI-only job).
- [ ] `MemoryPlanner` produces a coloring with N slots for a chain DAG (N=2 for the 5-node chain).
- [ ] `Pipeline::load` rejects a contrived 100 MP × 12 intermediates graph with a clear "peak exceeds device cap" error.
- [ ] `CPIPE_ENABLE_TRACY=ON` build links cleanly and emits zones in `Pipeline::run`, `Scheduler::dispatch_node`, `ComputeContext::submit_halide`, `BufferAllocator::create` (validated by capturing a Tracy session on the developer machine).

**Verification:**
- [ ] `ctest -R test_taskflow_parallel` green.
- [ ] `ctest -R test_interference_graph` green.

**Dependencies:** T6 (Vulkan handoff must work before parallel dispatch is meaningful).

**Files likely touched:**
- `src/cpipe/runtime/{Scheduler.cpp,MemoryPlanner.cpp,Pipeline.cpp}`
- `include/cpipe/runtime/MemoryPlanner.hpp`
- `src/cpipe/runtime/TracyShim.hpp`
- `tests/unit/{test_taskflow_parallel,test_interference_graph}.cpp`
- `vcpkg.json` (+tracy)

**Estimated scope:** L (4 source + 1 header + 2 tests + vcpkg).

---

### T12 — Golden Harness + E2E DNG → HEIF + Tag `v0.2`

**Description.** Author the golden harness under `tests/golden/`: `psnr.cpp` (~50-line manual PSNR helper, [PD-69](#4-phase-decisions-pd-n)) + `run_golden.cpp` (Catch2 driver). Populate per-node EXR golden pairs ([PD-64](#4-phase-decisions-pd-n)) and the e2e `pixel8pro_d65.dng` → `expected.heif` pair via `scripts/generate_golden.sh` ([PD-63](#4-phase-decisions-pd-n)). Author `tests/integration/test_dng_to_heif_e2e.cpp`: drives the full chain from `DngReader.read()` through `Pipeline::run()` against `tests/fixtures/min-pipeline.cpipe.json`; PSNR per-node ≥ 40 dB; e2e ≥ 35 dB. Run under both Debug (ASAN + UBSAN) and Release in CI. Tag `v0.2` once green per [PD-76](#4-phase-decisions-pd-n).

**Acceptance criteria:**
- [ ] `tests/fixtures/min-pipeline.cpipe.json` validates against `pipeline-v0.2.json`.
- [ ] `tests/integration/test_dng_to_heif_e2e.cpp` produces a HEIF file that PSNR-matches `expected.heif` within the threshold.
- [ ] Per-node golden tests (5 cases) all PSNR ≥ 40 dB.
- [ ] ASAN + UBSAN produce no first-party findings (Mesa / NVIDIA leaks silenced by lsan suppression file).
- [ ] CI green on `main` for ≥ 24 h (or per [PD-76](#4-phase-decisions-pd-n) override).
- [ ] Tag `v0.2` created and pushed; GitHub Release published.

**Verification:**
- [ ] `ctest --preset golden --output-on-failure` green under both Debug and Release.
- [ ] `git tag --list 'v0.2'` returns `v0.2`.
- [ ] GitHub Releases page shows `v0.2` with release notes; the latest five `main` CI runs are successful.

**Dependencies:** T10, T11.

**Files likely touched:**
- `tests/golden/{psnr.cpp,run_golden.cpp}`
- `tests/golden/<each-node>/{in,out}.exr` (LFS)
- `tests/golden/e2e_dng_to_heif/{in.dng,out.heif}` (LFS)
- `tests/fixtures/min-pipeline.cpipe.json`
- `tests/integration/test_dng_to_heif_e2e.cpp`
- `scripts/generate_golden.sh` (records RT CLI flags / RT version)

**Estimated scope:** L (≥ 6 binaries + 4 source files + script; mostly fixture authoring).

---

## 7. Architecture Notes (P1-specific)

These are P1 implementation specifics that do not warrant a new locked decision but are worth pinning so T1–T12 stay coherent.

- **`linearize` / `blacklevel` / `wb.dual_illuminant` are Bayer-domain.** Their output is `BufferKind::Image2D` with `PixelFormat::R16_SFLOAT` (single-channel FP16, [PD-83](#4-phase-decisions-pd-n)). The mosaic structure is preserved; nodes interpret pixel addresses through the `CFAPattern` carried in `DngMetadata`.
- **`demosaic.bilinear` is the colour transition.** Input is `R16_SFLOAT` (1 channel); output is `R16G16B16A16_SFLOAT` (4 channels, alpha = 1). The Halide generator branches on `cfa_pattern` so each pattern compiles into a static path.
- **`colormatrix.dng_to_working` caches the composed transform.** `Linearize` does not, but matrix nodes can amortize a 3×3 × 3×3 composition across millions of pixels by computing once in `create()` and storing in per-instance state ([P10 / Plugin SDK §8.2](plugin-sdk.md#82-create)).
- **`output.heif_sdr` is the *only* CPU-mediated heavyweight node in P1.** Its `process()` is allowed to be > 100 ms; Tracy zones make this visible.
- **`HeifWriter` produces both CICP nclx and ICC `prof` boxes** ([PD-49](#4-phase-decisions-pd-n), [PD-53](#4-phase-decisions-pd-n)). The dual-write rule comes from [`tech.md` §7](tech.md#7-layer-6--color--format); P1 follows it from day one so P2 HDR can flip CICP without retrofitting ICC.
- **DngReader is *not* a node** ([`buffer.md` §10.1](buffer.md#101-dngreader), [PD-67](#4-phase-decisions-pd-n)). The Pipeline JSON's top-level `inputs[]` block enumerates external producers; `Pipeline::run` resolves them and feeds the resulting `IBuffer*` into the first node's input port. The `architecture.md` §11 example is updated under [PD-82](#4-phase-decisions-pd-n) to match this contract.
- **Vulkan minimum is `linearize` only.** All other nodes (including `output.heif_sdr`) execute on CPU. The CPU↔GPU handoff therefore appears exactly twice in the pipeline: once on `linearize` entry (CPU staging → VulkanBuffer via VMA host-import) and once on `linearize` exit (VulkanBuffer → CPU read in `blacklevel`). Timeline value 1 marks the transition.
- **Halide v21 Vulkan target relies on SPIRV-Tools** which Halide fetches transitively; no additional vcpkg port is required. Cold rebuild after T2 takes ~30 min the first time; the binary cache absorbs subsequent runs ([PD-79](#4-phase-decisions-pd-n)).
- **`HalideVulkanAdapter` is host-only.** It constructs `halide_buffer_t` from a `VulkanBuffer` (VkBuffer handle + element type + dims + strides), invokes the AOT entry point, and never exposes any of this to the plugin (per [P14](plugin-sdk.md#1-decision-summary)).
- **Per-instance state for `wb.dual_illuminant` and `colormatrix.dng_to_working`** is precomputed in `create()`; the host passes initial params (which include relevant `DngMetadata` references) at construction time. The plugin SDK's `Result<void> create(const ParamView&)` is sufficient for this; no manifest extension is needed.

---

## 8. Tests in P1 (unit + integration + golden)

| # | Test                               | Layer       | Added in | Asserts                                                                                              |
|---|------------------------------------|-------------|----------|------------------------------------------------------------------------------------------------------|
| 1 | `test_pixel_format` *(P0 ext.)*    | unit        | T1       | `R16_SFLOAT` byte-width, `to_string`, round-trip                                                     |
| 2 | `test_dng_reader`                  | unit        | T3       | Reads `pixel8pro_d65.dng`; ActiveArea crop applied; metadata fields match `exiftool` reference        |
| 3 | `test_dng_opcode_meta`             | unit        | T3       | OpcodeList1/2 parser yields expected entry vector on synthetic byte streams; rejects Quad Bayer       |
| 4 | `test_precision_planner`           | unit        | T4       | Mismatched-precision edge fails load; matching graph succeeds; error message names the offending edge |
| 5 | `test_linearize_node`              | unit        | T5       | CPU Halide AOT path: 64×64 Bayer → FP16 normalized; matches LinearizationTable LUT within 1 ULP       |
| 6 | `test_vulkan_buffer`               | unit        | T6       | VMA-backed `VulkanBuffer` round-trips host pointer; correct stride / size                             |
| 7 | `test_halide_vulkan`               | unit        | T6       | `linearize_vulkan` AOT produces output equal to CPU variant within 1 ULP (Lavapipe + RTX)             |
| 8 | `test_timeline_semaphore`          | unit        | T6       | Linearize advances timeline 0 → 1; host wait succeeds; second wait returns immediately                |
| 9 | `test_blacklevel_node`             | unit        | T7       | Analytical BlackLevel + WhiteLevel rescaling produces FP16 within 1 ULP                                |
| 10 | `test_wb_dual_illuminant`         | unit        | T7       | CCT interpolation between StdA + D65 matches hand-computed reference (covers `CctInterp`)            |
| 11 | `test_demosaic_bilinear`          | unit        | T8       | All 4 CFA patterns: PSNR ≥ 60 dB vs analytical reference                                              |
| 12 | `test_colormatrix`                | unit        | T9       | ForwardMatrix2 + Bradford CAT round-trip; camera primary → Rec.2020 chromaticity within 1e-3          |
| 13 | `test_heif_writer`                | unit        | T10      | Synthetic input → HEIF → decode round-trip; CICP `1/1/1`; ICC valid; ΔE2000 ≤ 2                       |
| 14 | `test_ocio_lcms2`                 | unit        | T10      | `OcioContext` loads study config; `IccGenerator::sRGBv4` round-trip via `iccdump`                     |
| 15 | `test_taskflow_parallel`          | unit        | T11      | Independent nodes execute concurrently on TaskFlow workers; per-instance serialized                   |
| 16 | `test_interference_graph`         | unit        | T11      | 5-node chain colors to 2 slots; 100 MP × 12 intermediates fails peak pre-check                        |
| 17 | `test_dng_to_heif_e2e`            | integration | T12      | Full pipeline on `pixel8pro_d65.dng` → HEIF; PSNR ≥ 35 dB vs `expected.heif`; ASAN+UBSAN clean       |
| 18 | golden per-node × 5               | golden      | T12      | Per-node EXR PSNR ≥ 40 dB vs RT-bilinear reference                                                    |

Total P1 additions: 16 unit + 1 integration + 5 golden = **22 tests new**, on top of the 25 P0 tests → 47 tests at v0.2 tag. The [PD-26](phase-00-foundation.md#4-phase-decisions-pd-n) "no coverage gate" stance carries.

---

## 9. Risk Register (P1)

P1 carries six concrete risks. Inherited roadmap-level risks (R11, R12, R13) get a row alongside P1-specific ones per [the question batch](#) decision; the master register at [`research/00-summary.md §7`](research/00-summary.md#7-risk-register) is unchanged.

| #     | Risk                                                                                                                                          | Impact | Likelihood | Mitigation                                                                                                                                                              |
|-------|-----------------------------------------------------------------------------------------------------------------------------------------------|--------|------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| R11   | libheif LGPL dynamic-link path on Linux is the canonical path; vcpkg port may default to static.                                              | Medium | Low        | T10 verifies the vcpkg port flags `BUILD_SHARED_LIBS=ON` for libheif; `ldd` check in CI; PR review records the linkage outcome.                                          |
| R12   | TaskFlow + Vulkan integration first lands here. `halide_set_custom_do_par_for` redirecting Halide's CPU pool must not deadlock against TaskFlow. | High   | Medium     | T6 adds a stand-alone smoke test invoking a Halide AOT pipeline from inside a `tf::Executor` worker; deadlock check is part of `test_halide_vulkan`.                    |
| R13   | DNG 1.7 (Pixel 9-era) ships JPEG XL payloads. P1 explicitly rejects these.                                                                    | Low    | Confirmed  | [PD-39](#4-phase-decisions-pd-n) decision; `DngReader::read` returns `CPIPE_UNSUPPORTED`. The corpus DNG is DNG 1.4–1.6 only.                                                              |
| P1-R1 | Halide v21 Vulkan backend on Mesa Lavapipe is slow (~minutes on a small image) — CI Vulkan step may exceed 5 min.                            | Medium | Medium     | Lavapipe runs only on the smallest synthetic 64×64 test in `test_halide_vulkan`; the real Pixel 8 Pro corpus DNG goes through CPU for the e2e test (Vulkan is exercised only for the unit-level smoke). |
| P1-R2 | kvazaar yuv444p Main is less commonly tested than yuv420p Main10. Some HEIC readers may fail.                                                  | Low    | Medium     | T10 verifies decode via libde265 in CI + manual viewer check on GNOME / macOS Preview before tagging.                                                                    |
| P1-R3 | LibRaw 0.22 vcpkg port may not exist with that exact version when T3 lands.                                                                   | Medium | Medium     | T3 may need to land a `vcpkg/overlay-ports/libraw/` patch — the `vcpkg-configuration.json` scaffold from T2 already supports overlay ports.                              |

The roadmap-level risk register ([Research 00 §7](research/00-summary.md#7-risk-register)) is unchanged by P1. P1 exercises R11, R12, and R13 for the first time; R1–R10, R14–R15 stay deferred.

---

## 10. Definition of Done (verification commands)

Run these in order from a fresh clone of `github.com/xndcn/cpipe`. Each command's exit status is the gate.

```bash
# 1. Clone + bootstrap (LFS now required)
git clone https://github.com/xndcn/cpipe.git
cd cpipe
git lfs install
git lfs pull
pre-commit install

# 2. Configure + build (Debug; ASAN + UBSAN)
cmake --preset linux-debug
cmake --build --preset linux-debug -j

# 3. Run all tests under Debug
ctest --preset linux-debug --output-on-failure

# 4. Configure + build (Release)
cmake --preset linux-release-clang
cmake --build --preset linux-release-clang -j

# 5. Run all tests under Release (including golden)
ctest --preset linux-release-clang --output-on-failure
ctest --preset golden --output-on-failure

# 6. End-to-end smoke test
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro_d65.dng \
    -p tests/fixtures/min-pipeline.cpipe.json \
    -o /tmp/cpipe_p1_out.heif
heif-info /tmp/cpipe_p1_out.heif | grep -E 'colour profile.*nclx|primaries.*1|transfer.*1'
ls -l /tmp/cpipe_p1_out.heif    # non-empty

# 7. Visual viewer check (manual)
xdg-open /tmp/cpipe_p1_out.heif

# 8. CI status
gh run list --workflow=build-and-test.yml --branch=main --limit=5
#    => All five most-recent runs on main must be "completed success".

# 9. Tag
git tag -a v0.2 -m "cpipe v0.2 — Walking Skeleton"
git push origin v0.2
```

If commands 1–8 all return zero exit status and CI has been green for ≥ 24 h (or per [PD-76](#4-phase-decisions-pd-n) override), P1 is done.

---

## 11. Dependencies (vcpkg.json delta)

P1 vcpkg manifest at tag `v0.2`:

```json
{
  "name": "cpipe",
  "version-string": "0.2.0-alpha",
  "builtin-baseline": "<pinned-microsoft-vcpkg-commit>",
  "dependencies": [
    "taskflow",
    "catch2",
    "spdlog",
    "nlohmann-json",
    "nlohmann-json-schema-validator",
    "tl-expected",
    "cli11",

    "libraw",
    { "name": "libheif", "features": ["kvazaar"] },
    "kvazaar",
    "opencolorio",
    "lcms",
    "openimageio",
    "vulkan-memory-allocator",
    "vulkan-headers",
    "vulkan-loader",
    "tracy"
  ]
}
```

Halide stays on `FetchContent` per [`tech.md` §2](tech.md#2-layer-1--build--toolchain) — the Vulkan backend is enabled via `CPIPE_ENABLE_HALIDE_VULKAN=ON` ([PD-72](#4-phase-decisions-pd-n)). The baseline commit may be bumped in T2 if any P1 port requires a newer registry snapshot; the bump is recorded in this phase doc's PD history.

---

## 12. Out of Scope (P1)

Stated explicitly so contributors don't accidentally expand P1:

- RCD / AMaZE / VNG / PPG demosaic; X-Trans (P2 / out of v1).
- OpcodeList2 *execution* (GainMap / vignette / lens shading); OpcodeList3 (lens correction); Quad Bayer remosaic (all P2).
- HDR HEIF (PQ / HLG); UltraHDR; Apple Adaptive HDR (P2 / v1.1).
- Web Editor; cpipe-server; pairing; WS thumbnail/profile streams (P3).
- IQA harness (piq, pyiqa); 50-image corpus; Wilcoxon corpus gate; microbench harness (P3).
- AI nodes (NAFNet-w32, AdaInt 3D-LUT, Wronski burst); ExecuTorch / ONNX RT / QAIRT (P4 / v1.1).
- Android target; Camera2 NDK; AHB; Hexagon HTP (v1.1).
- macOS / iOS targets (v1.2 / v2).
- Tile-based / streaming / hot-reload / sandboxing / external `.so` plugins (v2).
- ICC v4 floatType / `lutAtoBfloatType` LUTs ([Research 13 §3.1](research/13-color-management.md)) — P1 uses lcms2's stock sRGB v4 only.
- Burst input (`cardinality: "array"`) — manifest schema reserves the field, but P1 only exercises `cardinality: "single"`.
- `param_changed` action; `cache_per_run`; `request_scratch` (P2+; see [`plugin-sdk.md` §12](plugin-sdk.md#12-v1-scope-limits)).
- Any node beyond the six listed in [§1](#1-objective).

---

## 13. What Shipped / What Slipped

*(Populated at tag time; placeholder until then.)*

**What shipped**

- TBD at end of T12.

**What slipped**

- TBD at end of T12. The roadmap's pressure-valve guidance in [`roadmap.md` §9](roadmap.md#9-slip-absorption-and-scope-pressure-valves) governs any P1 cut.

---

## 14. See Also

- [`roadmap.md`](roadmap.md) — overall phase plan, RD-NN decisions.
- [`phase-00-foundation.md`](phase-00-foundation.md) — predecessor; PD-1..PD-31 establish the build, ABI, and passthrough invariants this phase compounds on.
- [`architecture.md`](architecture.md) — six-target layout, threading model, lifecycle; §11 example updated for [PD-82](#4-phase-decisions-pd-n).
- [`buffer.md`](buffer.md) — `IBuffer` / `BufferLayout`; §3 PixelFormat extension for [PD-83](#4-phase-decisions-pd-n); §10.1 `DngReader` host class contract; §7.2 external import path.
- [`plugin-sdk.md`](plugin-sdk.md) — manifest schema; `CPIPE_REGISTER_NODE`; lifecycle actions; Halide AOT / Slang / Inference submission protocols.
- [`tech.md`](tech.md) — license verdicts and version pins for every P1 dependency.
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) §5 — interference-graph memory planner (full design implemented in T11).
- [`research/12-dng-format.md`](research/12-dng-format.md) — DNG metadata layout, OpcodeList1/2/3 semantics; basis for `DngReader` and `dng_opcode`.
- [`research/13-color-management.md`](research/13-color-management.md) — OCIO + lcms2 wiring; CICP / ICC dual-write rationale.
- [`research/14-heif-and-hdr-output.md`](research/14-heif-and-hdr-output.md) — libheif / kvazaar plugin model; CICP signalling.
- [`research/07-classic-isp-algorithms.md`](research/07-classic-isp-algorithms.md) — algorithm provenance for `linearize / blacklevel / wb.dual_illuminant / demosaic.bilinear / colormatrix`.
