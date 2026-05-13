# Phase 1 — Walking Skeleton

> Date: 2026-05-13 · Phase tag: `v0.2` · Parent: [`roadmap.md`](roadmap.md) · References: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`phase-00-foundation.md`](phase-00-foundation.md)

This document is the detailed plan for Phase 1 of the cpipe v1.0 roadmap. P1's purpose is to take a real Bayer DNG, run it through five canonical ISP nodes, and emit an SDR HEIF file — the first end-to-end "walking skeleton" of the cpipe runtime. It mirrors the structure of [`phase-00-foundation.md`](phase-00-foundation.md) and overlays the planning template from `agent-skills:planning-and-task-breakdown` (Overview / Architecture Decisions / Task List with checkpoints / Risks / Open Questions).

P0 shipped the empty target skeleton, the C ABI surface, the registry walk, and a Halide-AOT passthrough. P1 keeps everything P0 added and grows the runtime so that:

- A real DNG file (cropped Pixel 8 Pro, Bayer 2×2) is loaded via LibRaw + a first-party OpcodeList parser.
- Five ISP nodes (linearize → blacklevel → demosaic → wb → colormatrix) transform raw pixels into linear Rec.2020 D65 RGB FP16.
- One sink node encodes the result as an SDR HEIF (sRGB, ICC + CICP, kvazaar HEVC).
- The TaskFlow scheduler dispatches the DAG with real parallelism (validated against a diamond-shaped test DAG).
- Halide's Vulkan AOT path is wired through one node (demosaic) end-to-end, and the rest of the device plane (VkInstance / VkDevice / VMA / VulkanBuffer / VulkanImage / timeline semaphore) lives behind an `IDevicePlane` abstraction.
- Tracy spans cover the three runtime hot paths.
- Per-node golden EXR fixtures land under Git LFS; integration smoke compares HEIF re-decode PSNR against a RawTherapee 5.10 reference.

When P1 is done, the project is tagged `v0.2` and Phase 2 begins.

---

## 1. Objective

A working `cpipe run input.dng -p min-pipeline.cpipe.json -o out.heif` for a real Pixel 8 Pro Bayer DNG, executed through five canonical ISP stages plus the SDR HEIF output node, end-to-end on a Linux x86_64 + NVIDIA RTX + Vulkan 1.3 developer machine.

**Success looks like:**

- An `examples/pipelines/min-pipeline.cpipe.json` describing the 5-node ISP chain runs against `tests/corpus/pixel8pro.dng` and emits an `out.heif` that any modern viewer (e.g., GNOME Files thumbnail, macOS Photos) opens.
- Per-node EXR golden fixtures (5 of them) pass `ctest -L golden` with PSNR ≥ 40 dB against a RawTherapee 5.10 (bilinear demosaic) reference.
- Integration smoke re-decodes the produced HEIF with libde265, converts back to linear Rec.2020 D65 EXR, and asserts PSNR ≥ 37 dB against the same reference (DoD ±3 dB minus encoder loss budget).
- `Pipeline::load` rejects bad graphs (topology, memory peak, precision intersection) before any worker thread spins up.

P1 explicitly does *not* deliver: HDR PQ output, OCIO Looks menu, the remaining 13 classic nodes, AI nodes, Quad Bayer remosaic, the Web Editor, the IQA harness, or Slang shaders. Those are P2–P5.

---

## 2. Inputs

- P0 outputs: empty target skeleton, C ABI surface, registry walk, Halide CPU AOT passthrough, JSON Schemas, 11 unit tests, 1 integration smoke. All locked in `v0.1`.
- Locked design documents: [`architecture.md`](architecture.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`tech.md`](tech.md), [`roadmap.md`](roadmap.md).
- Development machine: Ubuntu 24.04+ with NVIDIA RTX-class GPU, Vulkan 1.3 driver (NVIDIA 570+ or Mesa 25), Vulkan validation layers (`vulkan-validation-layers` package), and an installed RawTherapee 5.10 (used only when refreshing golden fixtures locally).
- Git LFS installed locally and on the GitHub Actions runner.

---

## 3. Outputs

- Tag `v0.2` on a green main commit that satisfies §11 (DoD).
- `cpipe run *.dng -p *.cpipe.json -o *.heif` on Linux x86_64.
- `examples/pipelines/min-pipeline.cpipe.json` reference graph.
- `tests/corpus/pixel8pro.dng` (cropped, CC0/CC-BY) committed via Git LFS.
- Five EXR golden fixtures under `tests/golden/<node-id>/` (Git LFS).
- New native sources: `IDevicePlane` + `VulkanDevicePlane`, `VulkanBuffer`, `VulkanImage`, `MemoryPlanner`, `PrecisionPlanner` linked into `cpipe-runtime`; six new builtin nodes (`dng_input` + 5 ISP + output) registered in `cpipe-builtin-nodes`.
- New schema: `schemas/pipeline-v0.2.json`.

---

## 4. Phase Decisions (PD-N)

P1-specific decisions, locked from this planning round. PD numbering restarts at `PD-1` phase-local. Cross-references `RD-N / D-N / B-N / P-N` stay global; "carries P0-PD-NN" links to the prior phase's table.

| ID    | Decision                                | Value |
|-------|-----------------------------------------|-------|
| PD-1  | Phase doc structure                     | Mirrors `phase-00-foundation.md` (Objective / Inputs / Outputs / PD table / Repo Layout / Task List / Notes / Tests / Risk / DoD / What Shipped / Deps / OOS / See Also) overlaid with the `planning-and-task-breakdown` template (Overview / Architecture Decisions / Tasks-with-checkpoints / Risks / Open Questions). Language: English ([RD-28](roadmap.md#1-decision-quick-reference)). |
| PD-2  | Branch / PR policy                      | One PR per T-task (carries CLAUDE.md §Workflow + P0 policy). Sub-task PRs only if the T DoD closes before any acceptance box is ticked. |
| PD-3  | Git LFS bootstrap                       | P1 turns on Git LFS for `*.exr` and `*.dng`. `.gitattributes` adds the two filters; CI uses `actions/checkout@v4` with `lfs: true`. P2+ extends to `*.heif` / `*.icc` / `*.cube` lazily. |
| PD-4  | 24-h release bake                       | Continued waiver per P0-PD-37: `v0.2` ships when latest `main` CI is green on the release-candidate commit, plus pushed tag and GitHub Release. |
| PD-5  | New dependencies                        | `libraw`, `opencolorio`, `lcms`, `libheif`, `libde265`, `vulkan-memory-allocator`, `vulkan-headers`, `vulkan-loader`, `openimageio`, `tracy` from vcpkg. `kvazaar` via `vcpkg/overlay-ports/kvazaar/` (vcpkg lacks the port). Halide stays on FetchContent (carries P0-PD-31). |
| PD-6  | LGPL link discipline                    | `libheif` and `libde265` linked dynamically to honor the LGPL static-link clause and [`tech.md` §15](tech.md#15-license-posture-cliff-notes). The vcpkg manifest selects `libheif[hevc-decoder=libde265,hevc-encoder=kvazaar]`; CI verifies `ldd` does not list `libx265`. |
| PD-7  | Halide Vulkan target                    | `cmake/HalideHelpers.cmake` enables both `x86-64-linux` (CPU) and `host-vulkan` codegen. P1 nodes default to CPU; only `demosaic.bilinear` registers an additional Vulkan AOT variant. |
| PD-8  | PSNR computation                        | `OIIO::ImageBufAlgo::compare()` from OpenImageIO 2.5+. Test binaries link OIIO; `cpipe-runtime` does not. |
| PD-9  | Tracy spans                             | Three spans on the runtime hot path: `Pipeline::run`, `Scheduler::dispatch_node`, `ComputeContext::submit_halide`. `CPIPE_ENABLE_TRACY` defaults `OFF`; spans compile away when off. |
| PD-10 | OpenImageIO scope                       | OIIO is a *test-and-tooling* dep only; the production `cpipe-runtime` library does not link it. Goldens, PSNR, and example I/O are the only OIIO consumers. |
| PD-11 | Vulkan acquisition                      | `vulkan-headers` + `vulkan-loader` + `vulkan-memory-allocator` from vcpkg. Validation layers required at runtime in Debug only. |
| PD-12 | DngReader OpcodeList policy             | DngReader **reads** OpcodeList1 / LinearizationTable / OpcodeList2 / black/white-level / CFA. Raw OpcodeList1/2/3 wire bytes are stashed into `BufferMetadata.ext_blobs["com.cpipe.dng.opcode_list_*_bytes"]` (carries [B14](buffer.md#1-decision-summary) / [`buffer.md` §6.5](buffer.md#65-dng-opcodelist-relationship)). LinearizationTable is mapped into `CalibrationBlock.linearization_table`. **No OpcodeList execution in P1**; OpcodeList3 dispatcher is a P2 sub-domain. |
| PD-13 | Test DNG fixture                        | `tests/corpus/pixel8pro.dng` is a cropped real Pixel 8 Pro DNG sourced under CC0 / CC-BY (e.g. RawSamples.ch / Imaging Resource). Maximum 1920×1080. Committed via Git LFS. |
| PD-14 | DngReader CFA gate                      | DngReader rejects any non-2×2 Bayer mosaic (Quad Bayer 4×4, X-Trans, Foveon) with `CPIPE_FAILED` and a structured spdlog message. Quad Bayer remosaic is a P2 sub-domain. |
| PD-15 | OpcodeList parser namespace             | First-party OpcodeList parser under `cpipe::ingest::dng_opcode::*`; DngReader at `cpipe::ingest::dng::DngReader`. Notes drift from [`buffer.md` §11.1](buffer.md#111-dngreader)'s example `cpipe::ingest::DngReader`; the extra `dng/` segment isolates Camera2 (v1.1) from DNG. |
| PD-16 | Per-node golden corpus                  | Five EXR pairs (input + expected output), one per ISP node. Stored in `tests/golden/<node-id>/`. P1 does **not** ship a per-pipeline (end-to-end) golden EXR; integration smoke uses the encoded-then-decoded HEIF. |
| PD-17 | Per-node PSNR threshold                 | ≥ 40 dB (carries [RD-13](roadmap.md#1-decision-quick-reference)). |
| PD-18 | Demosaic algorithm in P1                | Only `com.cpipe.demosaic.bilinear`. RCD / AMaZE land in P2. Reference goldens are produced with **RawTherapee 5.10 set to bilinear demosaic** so the per-node PSNR floor is realistic. |
| PD-19 | Halide Vulkan target node               | `demosaic.bilinear` is the lone Vulkan AOT variant in P1. The other four classic nodes plus the output sink stay CPU AOT only. |
| PD-20 | Node ID names                           | `com.cpipe.linearize.dng_lut`, `com.cpipe.blacklevel.dng_levels`, `com.cpipe.demosaic.bilinear`, `com.cpipe.wb.dual_illuminant`, `com.cpipe.colormatrix.dng_to_working`, `com.cpipe.output.heif_sdr`, `com.cpipe.builtin.dng_input`. The `dng_*` suffixes mark "from DNG metadata" so v2 may add e.g. `linearize.cinema` without renames. |
| PD-21 | Buffer precision chain                  | DNG → R16_UINT (Bayer); linearize → R32_SFLOAT (Bayer FP, [0,1] after LUT); blacklevel → R32_SFLOAT (Bayer FP); demosaic → R16G16B16A16_SFLOAT (RGBA FP16); wb / colormatrix → R16G16B16A16_SFLOAT; output sink internally converts to 8-bit sRGB. |
| PD-22 | wb.dual_illuminant scope                | P1 implementation = invert `CaptureBlock.as_shot_neutral` per channel. ColorMatrix1/2 + ForwardMatrix1/2 interpolation deferred to P2 (same node ID). Manifest is stable across the upgrade. |
| PD-23 | colormatrix path                        | Hand-coded 3×3 matrix chain (camera-native → XYZ D50 [ColorMatrix1] → XYZ D65 [Bradford] → Rec.2020 D65). Halide AOT generator. P1 colormatrix does **not** call OCIO. |
| PD-24 | Node `params` policy                    | All five ISP nodes plus output have empty `params` lists. Adjustable parameters appear in P3 with the editor. |
| PD-25 | OCIO + lcms2 use sites                  | OCIO 2.4 and lcms2 2.16 are linked only by the output sink. OCIO performs Rec.2020 D65 → sRGB on output; lcms2 generates the embedded sRGB ICC profile. |
| PD-26 | OCIO config file                        | Project ships a minimal `share/cpipe/ocio/v0.1/config.ocio` with three roles: `raw_camera`, `scene_linear_rec2020`, `output_srgb`. The `CPIPE_OCIO_CONFIG` env var overrides; the CLI / CMake defaults to the bundled config. Looks (`Standard SDR / Standard HDR`) ship in P2. |
| PD-27 | Halide AOT TARGETS                      | All five ISP nodes target `x86-64-linux` (CPU AOT); `demosaic.bilinear` additionally targets `host-vulkan`. CPU SIMD width left at runner default — no AVX2 / AVX512 variants in P1 (carries P0-PD-36 spirit). |
| PD-28 | SDR HEIF color signaling                | `output.heif_sdr` writes both an embedded ICC v4 sRGB profile (lcms2) and a CICP NCLX descriptor `(1, 13, 1)` (BT.709 / sRGB OETF / BT.709 RGB matrix). |
| PD-29 | kvazaar encode preset                   | `--preset=medium`, 8-bit Main, 4:2:0 chroma. CRF / QP at kvazaar default (QP=22). |
| PD-30 | output.heif_sdr input pixel format      | `R16G16B16A16_SFLOAT` (alpha discarded inside the sink). Matches the upstream colormatrix output. |
| PD-31 | Pipeline schema bump                    | `schemas/pipeline-v0.2.json`. The `input` block becomes an array `[{ port, kind, format }]` so multi-frame inputs (P4) need no further break. P1 always carries one item: `{ "port": "raw", "kind": "Image2D", "format": "R16_UINT" }`. CLI populates dims at runtime. `pipeline-v0.1.json` is rejected at load with a clear "schema version mismatch" error. |
| PD-32 | dng_input plugin                        | `com.cpipe.builtin.dng_input` is a registered plugin node (manifest: 0 inputs, 1 output `raw` Image2D R16_UINT, params: `{ "path": string }`). Its `process()` calls `cpipe::ingest::dng::DngReader::read(params.path)` and attaches the produced IBuffer + metadata to `outputs[0]`. The ingest *implementation* (LibRaw + OpcodeListParser) stays as host code in `cpipe-runtime`; the plugin is a thin shim so the editor and registry walk see "DNG Source" naturally. |
| PD-33 | Source-binding API                      | `Pipeline::set_source(port_name, plugin_id, params)` is the runtime API the CLI uses to bind the `dng_input` plugin to the `raw` input port after `Pipeline::load`. The pipeline JSON does **not** list the source plugin; CLI binds at runtime. Empty `set_source` for any input port aborts the run. |
| PD-34 | Removal of P0 raw .bin path             | The P0 raw .bin loader and CLI route retire in P1. `cpipe run` only accepts `*.dng`. |
| PD-35 | Scheduler scope in P1                   | TaskFlow `tf::Taskflow` + `tf::Executor` (process-wide, `N = std::thread::hardware_concurrency() - 1`) drives true parallel topo dispatch. A diamond-shaped test DAG validates concurrent execution; the smoke pipeline itself is linear. |
| PD-36 | Memory planner                          | Linear-lifetime ("liveness in topo order") plan; peak vs device cap pre-check at `Pipeline::load`; actual allocation through VMA. Interference-graph coloring deferred to P2. |
| PD-37 | Precision planner                       | "Validate-and-abort" placeholder. Adjacent-port precision intersection enforced at load; mismatches abort with `CPIPE_BAD_PRECISION`. Auto-insertion of `precision_convert` deferred. |
| PD-38 | Device plane abstraction                | `IDevicePlane` host-only interface in `cpipe-runtime`; only implementation in P1 is `VulkanDevicePlane`. The CPU path stays inside `ComputeContext` directly (no device plane needed). Hexagon / Metal stubbed but not built. |
| PD-39 | Vulkan device selection                 | First Vulkan 1.3 PhysicalDevice that supports `VK_KHR_timeline_semaphore` (core 1.2) plus a GraphicsAndCompute queue family. Override via `CPIPE_VULKAN_DEVICE_INDEX`. Failure is `CPIPE_UNSUPPORTED` with a structured spdlog error. |
| PD-40 | GPU IBuffer subclasses                  | P1 ships `VulkanBuffer` (storage) and `VulkanImage` (sampled) only. `VulkanHostPtrBuffer` (host pointer import via `VK_EXT_external_memory_host`) deferred to P2. DngReader output is copied into a VMA-managed `VulkanImage` for demosaic's GPU run. |
| PD-41 | Synchronization primitives              | `VkSemaphore` (timeline) + `VkFence`. Single VkQueue per family per [Architecture §4](architecture.md#4-process-and-thread-model); GPU submissions originate from TaskFlow workers. |
| PD-42 | Vulkan validation layers                | `VK_LAYER_KHRONOS_validation` enabled by default in Debug builds; off in Release. Errors route through the runtime's spdlog sink. README documents the `vulkan-validation-layers` package install. |
| PD-43 | ctest organization                      | Goldens use Catch2 LABEL `golden`; integration uses LABEL `integration`. CI matrix unchanged from P0-PD-12; `ctest -L golden` becomes a new sub-step of the existing `test` job. No new ctest preset. |
| PD-44 | CI LFS strategy                         | `actions/checkout@v4` with `lfs: true`. LFS object cache via `actions/cache@v4` keyed on `vcpkg.json` hash + LFS pointer commits. |
| PD-45 | P1 CLI surface                          | Only `cpipe run`. `info / serve / bench / iqa / model` remain unshipped. |
| PD-46 | Integration PSNR threshold              | Smoke harness re-decodes `out.heif` (libde265), color-converts back to linear Rec.2020 D65 EXR (lcms2 + OCIO inverse), and asserts PSNR ≥ 37 dB against the RawTherapee reference (DoD ±3 dB minus encoder loss budget). |
| PD-47 | Cpipe ABI version bump                  | `CPIPE_ABI_MINOR` raised from 1 to 2. Two new suites (`metadata`, `metadata_builder`) become live; old (P0) plugins keep working — they never query the new suites. |
| PD-48 | Metadata suite scope                    | P1 wires the typed getters listed in [`plugin-sdk.md` §3](plugin-sdk.md#3-c-abi-cpipe_nodeh): `cpipe_calibration_view`, `cpipe_capture_view`, `cs_role`, `applied_steps`, `active_area`, `tensor_quant` (returns `Scheme::None`), `get_blob`. Builder mirrors the §3 list. |
| PD-49 | Risk register                           | Phase-local risks tracked in §10 (`P1-R1` … `P1-RN`); inherited risks from [`research/00-summary.md` §7](research/00-summary.md#7-risk-register) cited but not restated. |
| PD-50 | What Shipped / What Slipped flow        | Phase doc §12 + [`roadmap.md` §4](roadmap.md#4-phase-1--walking-skeleton-tag-v02) + [`README.md`](../README.md) "Current Status" updated in the same PR that pushes `v0.2`. |

---

## 5. Architecture Decisions (cross-cutting)

The PD table locks specific values; this short narrative explains the *why* for the cross-cutting choices.

- **Working color space discipline.** Per [`tech.md` §7](tech.md#7-layer-6--color--format) the working color space is linear Rec.2020 D65 FP16. In P1 the boundary lives between `colormatrix.dng_to_working` (writes the working space) and `output.heif_sdr` (converts out to sRGB display-referred). All intermediate ports declare `cs_role: scene_linear_rec2020` and the manifest validator enforces this; mismatches fail at load. PD-23 / PD-25.

- **Source-plugin shim, not a host-only ingest.** PD-32 / PD-33 keep ingest as host code (LibRaw must stay outside the plugin trust boundary in v2) but expose it through a manifest-bearing plugin so the editor and the registry walk see "DNG Source" naturally. The runtime supports `Pipeline::set_source(port, plugin_id, params)` so a future Camera2BufferProducer (v1.1) drops in via the same hook.

- **One Vulkan node, one device plane.** PD-19 / PD-38 / PD-39 cap the GPU surface to exactly what the DoD's "Halide Vulkan AOT path for at least one of the 5 nodes" requires. Building only `demosaic.bilinear` on Vulkan keeps the cross-device handoff cost (CPU→Vulkan→CPU) measurable and limits the device-plane footprint. Wider GPU adoption is P2's job.

- **Linear-lifetime memory plan.** PD-36 picks the simpler algorithm because the P1 DAG is linear. The interference-graph coloring described in [`architecture.md` §5.2](architecture.md#5-pipeline-lifecycle) / [Research 03 §5](research/03-heterogeneous-scheduler.md) is reserved for P2 once the DAG widens to 18 nodes. The peak-vs-cap pre-check is exercised today.

- **Schema bump to v0.2 with multi-frame friendly entry shape.** PD-31 expands the P0 single-input layout block to a port array. Burst (P4) needs N entry buffers; making the entry an array now spares another schema break.

- **Strict pre-execution validation.** Three validators at `Pipeline::load` (topology / memory peak / precision intersection) all fail before any worker thread spins up. This is the difference between "cpipe run crashes after 30 s" and "cpipe run rejects in 50 ms" — a key debug-ergonomics anchor for the P3 editor.

---

## 6. Repository Layout (P1 end-state, additions only)

```
cpipe/
├── .gitattributes                  # PD-3: *.exr / *.dng → LFS
├── examples/
│   └── pipelines/
│       └── min-pipeline.cpipe.json # 5 ISP + 1 sink reference graph
├── include/
│   └── cpipe/
│       ├── core/
│       │   ├── BufferMetadata.hpp  # PD-47
│       │   ├── CalibrationBlock.hpp
│       │   ├── CaptureBlock.hpp
│       │   ├── ByteBlob.hpp
│       │   └── TensorQuant.hpp
│       ├── ingest/
│       │   ├── dng/
│       │   │   ├── DngReader.hpp
│       │   │   └── ColorMatrix.hpp
│       │   └── dng_opcode/
│       │       ├── OpcodeList.hpp
│       │       └── LinearizationTable.hpp
│       └── runtime/
│           ├── DevicePlane.hpp     # PD-38
│           ├── VulkanDevicePlane.hpp
│           ├── VulkanBuffer.hpp
│           ├── VulkanImage.hpp
│           ├── MemoryPlanner.hpp
│           ├── PrecisionPlanner.hpp
│           └── Sync.hpp
├── schemas/
│   ├── node-v0.1.json              # unchanged
│   └── pipeline-v0.2.json          # PD-31
├── share/
│   └── cpipe/
│       └── ocio/
│           └── v0.1/
│               └── config.ocio     # PD-26
├── src/
│   └── cpipe/
│       ├── color/
│       │   └── HeifWriter.cpp      # PD-25 / PD-28 / PD-29
│       ├── core/
│       │   └── BufferMetadata.cpp
│       ├── ingest/
│       │   ├── dng/
│       │   │   ├── DngReader.cpp
│       │   │   └── ColorMatrix.cpp
│       │   └── dng_opcode/
│       │       └── OpcodeListParser.cpp
│       ├── nodes/
│       │   ├── dng_input/
│       │   │   ├── dng_input.cpp
│       │   │   └── dng_input.json
│       │   ├── linearize/
│       │   │   ├── linearize_dng_lut.cpp
│       │   │   ├── linearize_dng_lut.json
│       │   │   └── linearize_dng_lut_generator.cpp
│       │   ├── blacklevel/
│       │   ├── demosaic/
│       │   │   ├── demosaic_bilinear.cpp
│       │   │   ├── demosaic_bilinear.json
│       │   │   └── demosaic_bilinear_generator.cpp   # CPU + host-vulkan TARGETS
│       │   ├── wb/
│       │   ├── colormatrix/
│       │   └── output/
│       │       ├── heif_sdr.cpp
│       │       └── heif_sdr.json
│       └── runtime/
│           ├── VulkanDevicePlane.cpp
│           ├── VulkanBuffer.cpp
│           ├── VulkanImage.cpp
│           ├── MemoryPlanner.cpp
│           ├── PrecisionPlanner.cpp
│           ├── MetadataSuite.cpp
│           └── Sync.cpp
├── tests/
│   ├── corpus/
│   │   └── pixel8pro.dng           # PD-13 (LFS)
│   ├── golden/                     # LFS
│   │   ├── linearize.dng_lut/
│   │   ├── blacklevel.dng_levels/
│   │   ├── demosaic.bilinear/
│   │   ├── wb.dual_illuminant/
│   │   └── colormatrix.dng_to_working/
│   ├── integration/
│   │   ├── test_diamond_dag_parallel.cpp
│   │   └── test_min_pipeline_dng_to_heif.cpp
│   └── unit/
│       └── ...                     # see §9
└── vcpkg/
    └── overlay-ports/
        └── kvazaar/                # PD-5
```

---

## 7. Task List

Ten vertical tasks. Three checkpoints. Each task lands a complete, testable slice in dependency order.

### Phase 1.A — Foundation

#### T1 — Deps + cpipe-core BufferMetadata

**Description.** Bump `vcpkg.json` to add the P1 dependencies. Author the `vcpkg/overlay-ports/kvazaar/` port. Enable `host-vulkan` codegen in `cmake/HalideHelpers.cmake`. Implement `BufferMetadata` / `CalibrationBlock` / `CaptureBlock` / `TensorQuant` / `ByteBlob` types in `cpipe-core` per [`buffer.md` §6](buffer.md#6-buffermetadata).

**Acceptance criteria:**
- [ ] `vcpkg install` succeeds for the eleven new ports (libraw, opencolorio, lcms, libheif, libde265, vulkan-memory-allocator, vulkan-headers, vulkan-loader, openimageio, tracy + the kvazaar overlay).
- [ ] `BufferLayout` unchanged; `BufferMetadata` types compile in `cpipe-core` with no Vulkan / OCIO / Halide / OIIO includes.
- [ ] `cpipe-core` unit tests for default-constructed metadata round-trip; `MetadataBuilder` (host-side struct) freeze produces an immutable snapshot.
- [ ] `host-vulkan` Halide variant produces a no-op AOT build (one synthetic test generator) without breaking the CPU build.

**Verification:**
- [ ] `ctest -R test_buffer_metadata` green.
- [ ] `nm $(find . -name '*.a') | grep -E 'CalibrationBlock|CaptureBlock'` shows symbols in `libcpipe-core.a`.

**Dependencies:** None (after P0).

**Files likely touched:**
- `vcpkg.json`, `vcpkg/overlay-ports/kvazaar/{vcpkg.json,portfile.cmake,patches/}`.
- `cmake/HalideHelpers.cmake`.
- `include/cpipe/core/{BufferMetadata,CalibrationBlock,CaptureBlock,ByteBlob,TensorQuant}.hpp`.
- `src/cpipe/core/BufferMetadata.cpp`.
- `tests/unit/test_buffer_metadata.cpp`.

**Estimated scope:** L (deps refresh + ~5 headers + 1 source + tests).

---

#### T2 — VulkanDevicePlane

**Description.** Introduce `IDevicePlane` (host-only) with `VulkanDevicePlane` as the only P1 implementation. Initialize `VkInstance` (validation layers Debug-on, Release-off), select the first eligible PhysicalDevice (Vulkan 1.3 + timeline_semaphore + GraphicsAndCompute) with `CPIPE_VULKAN_DEVICE_INDEX` override, create `VkDevice` + VMA allocator + a single `VkQueue` per family + per-thread command pools. Implement `VulkanBuffer` and `VulkanImage` IBuffer subclasses. Add `VkSemaphore` (timeline) and `VkFence` host-side wrappers in `runtime/Sync`.

**Acceptance criteria:**
- [ ] `runtime::VulkanDevicePlane::create()` returns a usable device on the development RTX machine.
- [ ] `VulkanBuffer::lock_cpu` / `unlock_cpu` round-trips bytes correctly via VMA staging.
- [ ] `VulkanImage` allocates a 256×256 `R16_UINT` image, fills via `memcpy` on a VMA staging buffer, reads back, asserts identity.
- [ ] On a no-Vulkan machine (`VK_ICD_FILENAMES=` empty), `VulkanDevicePlane::create()` returns `CPIPE_UNSUPPORTED` with a structured spdlog error.
- [ ] Validation layers appear in spdlog only in Debug; absent in Release.

**Verification:**
- [ ] `ctest -R test_vulkan_device_plane` green on the development machine.
- [ ] `vkconfig` (manual) confirms validation layers loaded under Debug.

**Dependencies:** T1.

**Files likely touched:**
- `include/cpipe/runtime/{DevicePlane,VulkanDevicePlane,VulkanBuffer,VulkanImage,Sync}.hpp`.
- `src/cpipe/runtime/{VulkanDevicePlane,VulkanBuffer,VulkanImage,Sync}.cpp`.
- `tests/unit/test_vulkan_device_plane.cpp`.

**Estimated scope:** L (5 headers + 4 source + 1 test).

---

#### T3 — Plugin ABI metadata suite

**Description.** Bump `CPIPE_ABI_MINOR` to 2. Wire `cpipe_metadata_suite_v1` and `cpipe_metadata_builder_suite_v1` per [`plugin-sdk.md` §3](plugin-sdk.md#3-c-abi-cpipe_nodeh). Add the matching `cpipe::sdk::BufferMetadata` and `cpipe::sdk::MetadataBuilder` C++ wrappers per [`plugin-sdk.md` §6](plugin-sdk.md#6-c-sdk-cpipesdkhpp). Update the dispatch shim so that `process()` callbacks receive `out_metadata` builders default-initialized from `inputs[0]`.

**Acceptance criteria:**
- [ ] `host->get_suite("metadata", 1)` returns a non-null vtable; the same call for `"metadata_builder"` does the same.
- [ ] The P0 passthrough plugin still loads and runs (it never queries the new suites).
- [ ] A new unit-test plugin reads `inputs[0]->metadata().cs_role()` and writes `out_metadata[0]->add_applied_step("test")`; freezing produces a `BufferMetadata` with `cs_role` carried over and `applied_steps == ["test"]`.
- [ ] Manifest `requires_steps_applied` / `sets_steps_applied` validation rejects a bad combination at `Pipeline::load`.

**Verification:**
- [ ] `ctest -R test_metadata_suite` green.
- [ ] ABI compatibility test confirms the P0 passthrough plugin descriptor still loads.

**Dependencies:** T1.

**Files likely touched:**
- `include/cpipe/sdk/cpipe_node.h` (ABI bump + suite signatures).
- `include/cpipe/sdk/sdk.hpp` (BufferMetadata / MetadataBuilder facades).
- `src/cpipe/runtime/MetadataSuite.cpp`.
- `src/cpipe/runtime/Pipeline.cpp` (per-process metadata builder lifecycle).
- `tests/unit/test_metadata_suite.cpp`, `tests/unit/test_metadata_builder.cpp`.

**Estimated scope:** L (ABI surface + 5+ headers updated + 3 source + 2 tests).

---

#### T4 — Pipeline refactor

**Description.** Rebuild `Pipeline` / `Scheduler` from the P0 serial dispatcher into a true TaskFlow-driven graph. Implement linear-lifetime `MemoryPlanner` (peak vs cap pre-check + VMA allocations). Implement `PrecisionPlanner` (validate-and-abort intersection). Add `Pipeline::set_source(port, plugin_id, params)`. Migrate to `pipeline-v0.2.json` (PD-31). Add a diamond-shaped test DAG to validate parallel dispatch.

**Acceptance criteria:**
- [ ] `Pipeline::load` accepts pipeline-v0.2.json; rejects pipeline-v0.1.json with a clear "schema version mismatch" message.
- [ ] `Pipeline::set_source` binds a plugin to an input port; missing source aborts `run` with a structured error.
- [ ] Memory planner reports peak; setting `Pipeline::set_device_memory_cap(<small>)` causes `Pipeline::load` to fail with `CPIPE_OOM` if peak exceeds.
- [ ] Precision planner catches a mismatched-precision pipeline at load with `CPIPE_BAD_PRECISION`.
- [ ] Diamond DAG test (1-source fanout to 2 parallel mid-nodes, fanin to 1 sink) runs the two interior nodes concurrently when `N >= 2` worker threads.

**Verification:**
- [ ] `ctest -R test_pipeline_load` green.
- [ ] `ctest -R test_diamond_dag_parallel` shows ≥ 2-thread concurrency in a Tracy capture (manual once; assertion in test based on overlap).

**Dependencies:** T2, T3.

**Files likely touched:**
- `schemas/pipeline-v0.2.json`.
- `include/cpipe/runtime/{Pipeline,Scheduler,MemoryPlanner,PrecisionPlanner}.hpp`.
- `src/cpipe/runtime/{Pipeline,Scheduler,MemoryPlanner,PrecisionPlanner}.cpp`.
- `tests/unit/test_pipeline_load.cpp` (extends P0 fixture).
- `tests/integration/test_diamond_dag_parallel.cpp`.

**Estimated scope:** L (schema + 4 headers + 4 source + 2 tests).

---

### Checkpoint A — after T1–T4

- [ ] All four tasks merged; `main` is green.
- [ ] `cpipe-runtime` links against Vulkan + VMA; trivial Vulkan smoke runs.
- [ ] Plugin ABI minor 2; new metadata suites callable from a unit-test plugin; P0 passthrough still runs.
- [ ] `Pipeline::load` rejects pipeline-v0.1.json; loads pipeline-v0.2.json.
- [ ] Review: any unexpected library pulled into `cpipe-core` (must stay Vulkan- / Halide- / OCIO-free)? Halide cold build budget still ≤ 20 min on CI?

---

### Phase 1.B — ISP nodes (5 stages + output sink)

#### T5 — DNG ingest

**Description.** Implement `cpipe::ingest::dng::DngReader` (LibRaw 0.22) and `cpipe::ingest::dng_opcode::OpcodeListParser` (read-only; LinearizationTable mapped, OpcodeList1/2/3 raw bytes preserved as `ext_blobs`). Author `com.cpipe.builtin.dng_input` plugin (manifest + thin `process()` shim that calls DngReader). Reject non-2×2 Bayer with `CPIPE_FAILED`. Land `tests/corpus/pixel8pro.dng` (cropped, CC0/CC-BY) via Git LFS.

**Acceptance criteria:**
- [ ] `DngReader::read(path)` returns an `IBuffer` with `R16_UINT Image2D` layout, full `BufferMetadata` (calibration: ColorMatrix1/2 + ForwardMatrix1/2 + AsShotNeutral + black/white levels + LinearizationTable + CFA; capture: timestamp / iso / exposure / lens / orientation; applied_steps=[]; cs_role="raw_camera"; exif/xmp/icc shared blobs; opcode_list_*_bytes blobs).
- [ ] `OpcodeListParser` parses LinearizationTable correctly for the smoke fixture and a synthetic minimal DNG; ignores OpcodeList3 execution (raw bytes preserved).
- [ ] `dng_input` plugin appears in the registry; `Pipeline::set_source("raw", "com.cpipe.builtin.dng_input", {"path": "..."})` binds.
- [ ] Quad Bayer 4×4 DNG fed to DngReader returns `CPIPE_FAILED`; Foveon DNG also rejected.

**Verification:**
- [ ] `ctest -R test_dng_reader` green.
- [ ] `ctest -R test_opcode_list_parser` green.
- [ ] `cpipe run` over the smoke fixture (with later T6–T9 in place) reads metadata and writes a debug log.

**Dependencies:** T1, T3.

**Files likely touched:**
- `include/cpipe/ingest/dng/{DngReader,ColorMatrix}.hpp`.
- `include/cpipe/ingest/dng_opcode/{OpcodeList,LinearizationTable}.hpp`.
- `src/cpipe/ingest/dng/{DngReader,ColorMatrix}.cpp`.
- `src/cpipe/ingest/dng_opcode/OpcodeListParser.cpp`.
- `src/cpipe/nodes/dng_input/{dng_input.cpp,dng_input.json}`.
- `tests/unit/test_dng_reader.cpp`, `tests/unit/test_opcode_list_parser.cpp`.
- `tests/corpus/pixel8pro.dng` (LFS).

**Estimated scope:** L (4 headers + 4 source + 2 tests + 1 plugin shim + 1 LFS fixture).

---

#### T6 — linearize + blacklevel

**Description.** Implement `com.cpipe.linearize.dng_lut` (LinearizationTable applied per pixel; outputs R32_SFLOAT Bayer FP) and `com.cpipe.blacklevel.dng_levels` (subtract black, divide by white-black; outputs R32_SFLOAT Bayer FP in [0,1]). Both are CPU AOT Halide nodes. Land per-node EXR golden fixtures.

**Acceptance criteria:**
- [ ] Each node's `process()` reads required metadata fields (LinearizationTable / black_level / white_level) via `cpipe_metadata_suite_v1`.
- [ ] Each node sets `applied_steps += "linearization"` / `"black_white_scaling"` accordingly; manifest `sets_steps_applied` validates.
- [ ] `ctest -R test_node_linearize` and `test_node_blacklevel` pass.
- [ ] `ctest -L golden` PSNR ≥ 40 dB on each node's golden EXR pair.

**Verification:**
- [ ] Goldens in `tests/golden/linearize.dng_lut/{in,out}.exr` and `tests/golden/blacklevel.dng_levels/{in,out}.exr` (LFS) compare green via OIIO.

**Dependencies:** T4, T5.

**Files likely touched:**
- `src/cpipe/nodes/linearize/{linearize_dng_lut.cpp,linearize_dng_lut.json,linearize_dng_lut_generator.cpp}`.
- `src/cpipe/nodes/blacklevel/{blacklevel_dng_levels.cpp,blacklevel_dng_levels.json,blacklevel_dng_levels_generator.cpp}`.
- `tests/unit/test_node_linearize.cpp`, `tests/unit/test_node_blacklevel.cpp`.
- `tests/golden/linearize.dng_lut/{in,out}.exr`, `tests/golden/blacklevel.dng_levels/{in,out}.exr` (LFS).

**Estimated scope:** M (6 source files + 2 tests + 4 LFS goldens).

---

#### T7 — demosaic.bilinear (CPU + Vulkan)

**Description.** Implement `com.cpipe.demosaic.bilinear` Halide generator targeting both `x86-64-linux` and `host-vulkan`. The CPU path is the default; the Vulkan path is selected when the runtime advertises a Vulkan device. Drop CFA from the output metadata; transition output to `R16G16B16A16_SFLOAT`.

**Acceptance criteria:**
- [ ] `add_halide_library` produces a CPU `.o` and a host-vulkan SPIR-V variant for the same generator.
- [ ] `ComputeContext::submit_halide("demosaic_bilinear", ...)` runs on CPU when `CPIPE_VULKAN_DEVICE_INDEX=-1`.
- [ ] Same call runs on Vulkan when device available; Tracy capture shows the `submit_halide` span on Vulkan device.
- [ ] Per-node golden PSNR ≥ 40 dB on CPU; if Vulkan device available, Vulkan path matches CPU within 1 ULP per channel (separate test).
- [ ] `out_metadata.clear_cfa()` and `add_applied_step("demosaic")` validated by manifest at freeze.

**Verification:**
- [ ] `ctest -R test_node_demosaic_bilinear` green.
- [ ] `ctest -R test_node_demosaic_bilinear_vulkan` green when `CPIPE_VULKAN_AVAILABLE=ON`.

**Dependencies:** T2, T6.

**Files likely touched:**
- `src/cpipe/nodes/demosaic/{demosaic_bilinear.cpp,demosaic_bilinear.json,demosaic_bilinear_generator.cpp}`.
- `tests/unit/test_node_demosaic_bilinear.cpp`.
- `tests/golden/demosaic.bilinear/{in,out}.exr` (LFS).

**Estimated scope:** M (3 source + 2 tests + 2 LFS goldens; Halide TARGETS list grows).

---

#### T8 — wb + colormatrix

**Description.** Implement `com.cpipe.wb.dual_illuminant` (P1 minimal: invert AsShotNeutral per channel) and `com.cpipe.colormatrix.dng_to_working` (hand-coded 3×3 chain → linear Rec.2020 D65). Both CPU AOT.

**Acceptance criteria:**
- [ ] wb node produces `applied_steps += "white_balance"`; output is white-balanced RGBA FP16.
- [ ] colormatrix node produces `applied_steps += "color_matrix"`, `cs_role = "scene_linear_rec2020"`; output is in working color space.
- [ ] Per-node golden PSNR ≥ 40 dB on each.
- [ ] If upstream metadata's `cs_role != "raw_camera"`, the colormatrix manifest's `requires_steps_applied` rejects load.

**Verification:**
- [ ] `ctest -R test_node_wb` green; `ctest -R test_node_colormatrix` green.

**Dependencies:** T7.

**Files likely touched:**
- `src/cpipe/nodes/wb/{*.cpp,*.json,*_generator.cpp}`.
- `src/cpipe/nodes/colormatrix/{*.cpp,*.json,*_generator.cpp}`.
- `tests/unit/test_node_wb.cpp`, `tests/unit/test_node_colormatrix.cpp`.
- `tests/golden/wb.dual_illuminant/{in,out}.exr`, `tests/golden/colormatrix.dng_to_working/{in,out}.exr` (LFS).

**Estimated scope:** M (6 source + 2 tests + 4 LFS goldens).

---

#### T9 — output.heif_sdr

**Description.** Implement `com.cpipe.output.heif_sdr`: input `R16G16B16A16_SFLOAT` (Rec.2020 D65) → OCIO `ColorSpaceTransform` → 8-bit sRGB → libheif + kvazaar (preset=medium, 8-bit Main, 4:2:0). Embed sRGB v4 ICC (lcms2) and CICP NCLX `(1, 13, 1)`. Author the bundled `share/cpipe/ocio/v0.1/config.ocio`.

**Acceptance criteria:**
- [ ] Smoke test produces `out.heif`; `heif-info out.heif` confirms 8-bit Main + sRGB ICC + CICP=(1,13,1).
- [ ] CI re-decode test (libde265 → EXR via `cpipe::color::HeifReader` test helper) opens the file with no error.
- [ ] Verifies in Debug that the linkage closure does not include `libx265` (`ldd $cpipe-cli | grep -v x265`).
- [ ] OCIO `scene_linear_rec2020` ↔ `output_srgb` round-trip on a synthetic gradient is identity within 1 LSB.

**Verification:**
- [ ] `ctest -R test_output_heif_sdr` green.

**Dependencies:** T8.

**Files likely touched:**
- `share/cpipe/ocio/v0.1/config.ocio`.
- `src/cpipe/color/HeifWriter.cpp`, `src/cpipe/color/HeifReader.cpp` (test-only helper).
- `src/cpipe/nodes/output/{heif_sdr.cpp,heif_sdr.json}`.
- `tests/unit/test_output_heif_sdr.cpp`.

**Estimated scope:** L (1 OCIO config + 4 source + 1 test + dynamic-link discipline checks).

---

### Checkpoint B — after T5–T9

- [ ] All five ISP nodes plus output sink merged.
- [ ] Per-node golden tests green (`ctest -L golden`).
- [ ] `cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/min-pipeline.cpipe.json -o /tmp/out.heif` produces a non-empty HEIF that any viewer opens (manual smoke).
- [ ] No new dependencies pulled outside the PD-5 list.

---

### Phase 1.C — Glue + ship

#### T10 — Tracy + CLI + integration smoke + tag

**Description.** Wire Tracy spans (PD-9). Land `examples/pipelines/min-pipeline.cpipe.json`. Update CLI to detect `*.dng` and bind via `Pipeline::set_source`. Author the integration smoke `tests/integration/test_min_pipeline_dng_to_heif.cpp` (re-decode + PSNR ≥ 37 dB). Update `roadmap.md` and `README.md`. Tag `v0.2`.

**Acceptance criteria:**
- [ ] `cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/min-pipeline.cpipe.json -o /tmp/out.heif` exits 0; `heif-info` valid.
- [ ] `ctest -R test_min_pipeline_dng_to_heif` PSNR ≥ 37 dB.
- [ ] Tracy capture (`-DCPIPE_ENABLE_TRACY=ON` build) shows the three spans on a smoke run.
- [ ] `roadmap.md §4` P1 row updated to "shipped"; `README.md` "Current Status" mirrors it; `phase-01-walking-skeleton.md §12` "What Shipped / What Slipped" filled in.
- [ ] `git tag --list 'v0.2'` returns `v0.2`; GitHub Release notes attached.

**Verification:**
- [ ] DoD §11 commands all green.

**Dependencies:** T9.

**Files likely touched:**
- `apps/cli/main.cpp` (DNG suffix dispatch + `Pipeline::set_source` call).
- `examples/pipelines/min-pipeline.cpipe.json`.
- `tests/integration/test_min_pipeline_dng_to_heif.cpp`.
- `docs/roadmap.md`, `docs/phase-01-walking-skeleton.md` (this file's §12), `README.md`.
- `CMakeLists.txt` / `cmake/CompilerOptions.cmake` (Tracy enable flag).

**Estimated scope:** M (1 example + 1 integration test + CLI changes + doc updates).

---

### Checkpoint C — P1 DoD

- [ ] §11 verification commands all green.
- [ ] Latest `main` CI green on the release-candidate commit (PD-4 waiver carries P0-PD-37).
- [ ] No regressions on the 11 P0 unit / integration tests.
- [ ] `v0.2` tag pushed; GitHub Release published.

---

## 8. Architecture Notes (P1-specific)

- **Halide custom_par_for redirect.** `halide_set_custom_do_par_for` is bound at runtime init to the cpipe `tf::Executor` so Halide CPU AOT runs reuse the project-wide pool ([Architecture §4](architecture.md#4-process-and-thread-model)). Carries P0 T4 wiring; PD-9 spans land on top.
- **CPU↔Vulkan handoff for demosaic.** `ComputeContext::submit_halide` queries the active node's manifest `compute.engine` + `device`; for `demosaic.bilinear` with a Vulkan device available, it copies the upstream R32_SFLOAT Bayer buffer into a `VulkanImage` (VMA staging path), runs the `host-vulkan` Halide AOT, and copies the result back to a `R16G16B16A16_SFLOAT VulkanImage` for downstream CPU consumption. Real zero-copy via `VK_EXT_external_memory_host` is a P2 topic (PD-40 reservation).
- **OCIO config bundling.** `share/cpipe/ocio/v0.1/config.ocio` is copied into the build install prefix and located via `CPIPE_OCIO_CONFIG`. The CLI sets the env var to the bundled path if not externally specified. The config has only three roles (`raw_camera`, `scene_linear_rec2020`, `output_srgb`) and one `ColorSpaceTransform` between the latter two; Looks ship in P2.
- **Pipeline schema migration.** `pipeline-v0.1.json` users see "schema version mismatch" at load. CLI fails loud — there is no auto-migration. P0's smoke test fixtures move from `pipeline-v0.1.json` to `pipeline-v0.2.json` in T4 / T10.
- **dng_input plugin process() body.** The plugin shim `process()` reads `params.path` (string), calls `cpipe::ingest::dng::DngReader::read(path)`, attaches the produced IBuffer to `outputs[0]`, and the host metadata builder freezes the metadata produced by DngReader. The plugin does not link LibRaw directly; LibRaw is in `cpipe-runtime`'s linkage closure (host code).
- **Vulkan validation noise.** In Debug builds, validation layers route through spdlog with a per-message `vk_validation` tag; the runtime down-grades `INFO`-level layer messages to `trace` to keep CI logs readable.
- **Diamond DAG construction.** The parallel-dispatch test DAG is `dng_input → split_passthrough → {wb_lo, wb_hi} → fuse_passthrough` using P0's Passthrough plus two short-circuit "wb" stand-ins; the fuse node averages. The intent is to validate that two nodes with overlapping execution windows actually overlap in TaskFlow's worker pool, not to produce ISP-meaningful output.

---

## 9. Tests in P1 (additions to P0)

| #   | Test                                       | Layer       | Asserts |
|-----|--------------------------------------------|-------------|---------|
| 13  | `test_buffer_metadata`                     | unit        | default-construct / copy / mutate via builder; immutable freeze |
| 14  | `test_vulkan_device_plane`                 | unit        | VkInstance / VkDevice creation; VulkanBuffer round-trip; VkSemaphore timeline wait |
| 15  | `test_metadata_suite`                      | unit        | C ABI suite negotiation; reading typed fields and blobs |
| 16  | `test_metadata_builder`                    | unit        | builder default-init from `inputs[0]`; freeze produces correct `shared_ptr` |
| 17  | `test_pipeline_load`                       | unit        | pipeline-v0.2.json parses; v0.1 rejected; memory peak vs cap pre-check; precision intersection |
| 18  | `test_dng_reader`                          | unit        | Pixel 8 Pro fixture decoded; metadata fields populated; Quad Bayer rejected |
| 19  | `test_opcode_list_parser`                  | unit        | LinearizationTable parsed; OpcodeList3 raw bytes preserved |
| 20  | `test_node_linearize`                      | unit        | LUT applied; output FP normalization correct |
| 21  | `test_node_blacklevel`                     | unit        | (raw - black) / (white - black) per channel |
| 22  | `test_node_demosaic_bilinear`              | unit        | bilinear demosaic on synthetic 16×16 Bayer (CPU) |
| 23  | `test_node_demosaic_bilinear_vulkan`       | unit        | same generator on Vulkan; output matches CPU within 1 ULP |
| 24  | `test_node_wb`                             | unit        | AsShotNeutral inverse multiply per channel |
| 25  | `test_node_colormatrix`                    | unit        | 3×3 chain produces Rec.2020 D65 from camera-native input |
| 26  | `test_output_heif_sdr`                     | unit        | HEIF written; ICC + CICP present; libde265 re-decode succeeds; no libx265 in linkage |
| 27  | `test_diamond_dag_parallel`                | integration | TaskFlow shows ≥ 2 nodes concurrent on a diamond DAG |
| 28  | `test_min_pipeline_dng_to_heif`            | integration | end-to-end smoke; PSNR ≥ 37 dB |

| Label | Tests                  | Purpose |
|-------|------------------------|---------|
| `unit` | 13–26                  | per-component invariants |
| `integration` | 27, 28          | full pipeline behaviors |
| `golden` | 5 (one per ISP node, attached to tests 20–25) | per-node EXR golden PSNR ≥ 40 dB |

P0's 11 tests (1–12 numbering retained from `phase-00-foundation.md` §8) all continue to pass; their counts overlap because P0 had 12 entries.

---

## 10. Risk Register (P1-only)

| #     | Risk                                                                                                                       | Impact | Likelihood | Mitigation |
|-------|-----------------------------------------------------------------------------------------------------------------------------|--------|------------|------------|
| P1-R1 | Halide v21's Vulkan codegen produces a SPIR-V binary that fails on the CI runner's Vulkan driver (no consumer GPUs in CI).  | High   | Medium     | T7 keeps a CPU-only fallback default on CI. The Vulkan unit test is conditional on `CPIPE_VULKAN_AVAILABLE`. The dev machine catches Vulkan regressions; CI gates only the CPU path. |
| P1-R2 | LibRaw 0.22 misses some Pixel 8 Pro DNG fields the OpcodeListParser depends on (e.g. unusual LinearizationTable variants).  | Medium | Medium     | T5 acceptance tests both a real Pixel 8 Pro DNG and a hand-authored synthetic minimal DNG; the parser logs and degrades gracefully when the table is absent. |
| P1-R3 | libheif's vcpkg port pulls a static `libx265` in by default, which is GPL-trapped.                                          | High   | High       | PD-5 / PD-6: the manifest uses `libheif[hevc-decoder=libde265,hevc-encoder=kvazaar]`; CI verifies `ldd build/.../libheif.so` does not list `libx265`; manual license review before T9 lands. |
| P1-R4 | OCIO config alignment between `colormatrix.dng_to_working` (hand-coded 3×3, no OCIO) and `output.heif_sdr` (OCIO inverse-by-config) drifts. | Medium | Medium     | T8 ships a `cs_role` integration test that ensures the colormatrix node's hand-coded primaries match the OCIO config's `scene_linear_rec2020` definition (round-trip on a synthetic patch). |
| P1-R5 | RawTherapee 5.10 bilinear-demosaic reference and our Halide bilinear differ by ≥ 1% chroma due to subtle LUT vs convolution differences. | Medium | Low        | T7 acceptance test runs both on the same input and asserts PSNR ≥ 40 dB; if it fails, regenerate goldens with our own bilinear and downgrade to "self-reference" (logged as a phase deviation). |

Inherited risks ([Research 00 §7](research/00-summary.md#7-risk-register)) carried into P1: **R11** (libheif LGPL — first exercised here per PD-6), **R12** (TaskFlow + Vulkan integration — first exercised in T2 / T7), **R13** (JPEG XL DNG — accepted as v1 read-only).

---

## 11. Definition of Done (verification commands)

Run these in order from a fresh clone of `github.com/xndcn/cpipe`. Each command's exit status is the gate.

```bash
# 1. Clone + bootstrap
git clone https://github.com/xndcn/cpipe.git
cd cpipe
git lfs install && git lfs pull
pre-commit install

# 2. Configure + build (Debug)
cmake --preset linux-debug
cmake --build --preset linux-debug -j

# 3. Run all tests under Debug (ASAN + UBSAN)
ctest --preset linux-debug --output-on-failure

# 4. Configure + build (Release)
cmake --preset linux-release-clang
cmake --build --preset linux-release-clang -j

# 5. Run all tests under Release
ctest --preset linux-release-clang --output-on-failure

# 6. Per-node golden (5 nodes, PSNR ≥ 40 dB)
ctest --preset linux-release-clang -L golden --output-on-failure

# 7. Integration smoke (DNG → HEIF, PSNR ≥ 37 dB)
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro.dng \
    -p examples/pipelines/min-pipeline.cpipe.json \
    -o /tmp/cpipe_p1_out.heif
heif-info /tmp/cpipe_p1_out.heif | grep -E "color profile|nclx|matrix coefficients"

# 8. CI status
gh run list --workflow=build-and-test.yml --branch=main --limit=5
#    => All five most-recent runs on main must be "completed success".

# 9. Tag
git tag -a v0.2 -m "cpipe v0.2 — Walking Skeleton"
git push origin v0.2

# 10. GitHub Release
gh release create v0.2 --verify-tag --generate-notes --title "cpipe v0.2 - Walking Skeleton"
```

If commands 1–10 all return zero exit status and latest `main` CI is green on the release-candidate commit, P1 is done. The 24-hour bake remains waived per PD-4 (carries P0-PD-37).

---

## 12. What Shipped / What Slipped

> Authored by the closing PR (T10). Update `roadmap.md` §4 and `README.md` "Current Status" in the same commit.

**What Shipped.** *(to be authored in T10)*

**What Slipped.** *(to be authored in T10)*

---

## 13. Dependencies (vcpkg.json + FetchContent additions)

P1 vcpkg additions (PD-5, PD-6):

```json
{
  "name": "cpipe",
  "version-string": "0.2.0-alpha",
  "builtin-baseline": "<refresh at T1>",
  "dependencies": [
    "taskflow", "catch2", "spdlog", "nlohmann-json", "json-schema-validator",
    "tl-expected", "cli11",
    "libraw",
    {
      "name": "libheif",
      "default-features": false,
      "features": ["hevc-decoder", "hevc-encoder"]
    },
    "libde265",
    "kvazaar",
    "opencolorio", "lcms",
    "vulkan-memory-allocator", "vulkan-headers", "vulkan-loader",
    "openimageio",
    "tracy"
  ]
}
```

`kvazaar` enters via `vcpkg/overlay-ports/kvazaar/` (PD-5). Halide v21 stays on FetchContent (PD-7 / P0-PD-31).

`libheif` and `libde265` are dynamically linked (PD-6). The manifest's `default-features: false` block keeps any GPL-only `libx265` decoder/encoder feature out of the closure; CI guards via `ldd`.

---

## 14. Open Questions

P1 carries no globally-new open questions — all unresolved Q-NN are tracked in [`architecture.md` §17](architecture.md#17-open-questions). The following items are *implicitly* open inside P1 and tracked locally; if any of them surfaces as a hard blocker, escalate by adding a new PD row in this file.

- **OCIO inverse round-trip fidelity.** Whether OCIO `scene_linear_rec2020 → output_srgb → scene_linear_rec2020` round-trips identity on the integration smoke fixture is not pre-verified. T9 acceptance includes a synthetic-gradient round-trip; if it fails, fall back to a hand-coded inverse matrix in `HeifReader` test helper.
- **Pixel 8 Pro DNG variation.** Different Pixel 8 Pro firmware versions emit different OpcodeList3 / GainMap shapes. P1 reads but does not execute OpcodeList3, so this is not a blocker — but fixture stability matters for goldens. T5 commits one specific firmware's DNG; goldens are regenerated when the fixture is replaced.
- **Vulkan driver matrix on CI runners.** GitHub-hosted runners ship without Vulkan-capable GPUs. T7's Vulkan unit test is conditional; CI does not gate Vulkan correctness. A self-hosted Vulkan runner is a P3 topic.

---

## 15. Out of Scope (P1)

Stated explicitly so contributors don't accidentally expand P1:

- All non-bilinear demosaic algorithms (RCD, AMaZE) — P2.
- HDR PQ output, OCIO Looks menu — P2.
- Quad Bayer remosaic / direct 4×4 demosaic — P2 / v1.2.
- AI nodes (NAFNet, AdaInt, Wronski) — P4.
- Web Editor, IQA harness, 50-image corpus, microbench harness — P3.
- Slang shaders + slang-rhi integration — P2+.
- ExecuTorch / ONNX Runtime / QAIRT — P4 / v1.1.
- Camera2 / Android target — v1.1.
- Adobe DNG SDK ingest path — Q1 deferred.
- Hexagon / Metal device planes — v1.1 / v2.
- DNG re-export (round-tripping OpcodeList raw bytes back out) — v2.
- macOS / Windows / iOS targets.
- Editor-side authoring of new node types — Q15 resolved no for v1.
- External `.so` plugin loading — D4 reservation; v2.

---

## 16. See Also

- [`roadmap.md`](roadmap.md) §4 — P1 phase row + RD-NN decisions.
- [`phase-00-foundation.md`](phase-00-foundation.md) — preceding phase doc.
- [`architecture.md`](architecture.md) — six-target layout, threading model, lifecycle, editor protocol.
- [`buffer.md`](buffer.md) §6 — `BufferMetadata` (implemented in T1 / T3).
- [`plugin-sdk.md`](plugin-sdk.md) §3 — `cpipe_metadata_suite_v1` (implemented in T3).
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — full scheduler design (P1 implements the linear subset).
- [`research/12-dng-format.md`](research/12-dng-format.md) — basis for DngReader.
- [`research/13-color-management.md`](research/13-color-management.md) — basis for color management in P1.
- [`research/14-heif-and-hdr-output.md`](research/14-heif-and-hdr-output.md) — basis for output.heif_sdr.
