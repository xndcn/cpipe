# Phase 2 — Classic Nodes + HDR

> Date: 2026-05-15 · Phase tag: `v0.3` · Parent: [`roadmap.md`](roadmap.md) · References: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`phase-01-walking-skeleton.md`](phase-01-walking-skeleton.md)

This document is the detailed plan for Phase 2 of the cpipe v1.0 roadmap. P2's purpose is to grow the v0.2 walking skeleton into the full classic-ISP DAG: all 18 classic nodes from [Research 07](research/07-classic-isp-algorithms.md), DNG OpcodeList2/3 lens correction, Quad Bayer remosaic, the HDR (PQ) HEIF output path, and the OCIO Looks menu (`Standard SDR` / `Standard HDR`). It mirrors the structure of [`phase-01-walking-skeleton.md`](phase-01-walking-skeleton.md) and overlays the planning template from `agent-skills:planning-and-task-breakdown` (Overview / Architecture Decisions / Task List with checkpoints / Risks / Open Questions).

P1 shipped the synthetic-and-real Pixel 8 Pro DNG → 5-node → SDR HEIF walking skeleton on Linux, the metadata ABI suites, the Vulkan device-plane foundation, and self-referenced golden EXR pairs for the five ISP nodes ([PD-69](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [PD-70](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) carry RawTherapee 5.10 slips; [PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) carries the cpipe-owned Vulkan dispatch slip). P2 keeps everything P1 added and grows the runtime so that:

- The compute suite carries parameter buffers (`submit_halide_with_params`) so metadata-driven Halide AOT nodes (linearize, blacklevel, wb, colormatrix, BM3D, GainMap, …) no longer have to run as CPU plugin loops ([PD-56](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [PD-60](phase-01-walking-skeleton.md#4-phase-decisions-pd-n)).
- cpipe owns the Vulkan queue (`VulkanCommandBuffer::submit` + timeline-semaphore wait), retiring [PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n).
- The memory planner upgrades from linear-lifetime to interference-graph coloring ([Architecture §5.2](architecture.md#5-pipeline-lifecycle) / [Research 03 §5](research/03-heterogeneous-scheduler.md)).
- The precision planner auto-inserts `precision_convert` nodes at adjacent-port mismatches.
- Demosaic family lands: RCD (default, re-implemented from Sanz Rodríguez 2014), AMaZE (low-ISO, re-implemented from Martinec's public description), bilinear (P1 carry).
- OpcodeList2 GainMap (4×4 per-Bayer-channel for Quad Bayer + 2×2 for regular Bayer) sits between `linearize` and `blacklevel` per DNG spec semantics.
- OpcodeList3 dispatcher (`WarpRectilinear`, `FixVignetteRadial`, `FixBadPixelsConstant`, `FixBadPixelsList`, `TrimBounds`) sits after `demosaic`.
- WB upgrades to full dual-illuminant CCT interpolation (in-place on the P1 node ID `com.cpipe.wb.dual_illuminant`); `wb.greyworld_auto` helper joins for AsShotNeutral-missing DNGs.
- Tone family lands: `filmic_rgb` (default global), `mertens_local` (SDR local; HDR variant deferred to v1.1 per Research 07 OQ 6), `aces_filmic` (alt), `reinhard` (debug).
- Denoise family lands: `bm3d` (primary, Halide AOT), `guided_filter` (preview helper, GPU friendly), `wavelet_bayes_shrink` (chroma).
- `sharpen.edge_aware_usm` (guided-filter USM) lands.
- `color.3d_lut` lands with `.cube` / `.spi3d` loaders and tetrahedral interpolation default.
- `color.scene_linear_to_display` is extracted as a new node with `target` enum (`sRGB` / `BT2020-PQ` for P2; `DisplayP3` / `BT2020-HLG` reserved for v1.1) and walks OCIO via the Vulkan compute path. `output.heif_sdr` is refactored to be a pure encoder; `output.heif_hdr_pq` joins.
- The bundled OCIO config bumps to `share/cpipe/ocio/v0.2/config.ocio` with two Looks (`Standard SDR` / `Standard HDR`) and four displays.
- Pipeline JSON schema bumps to `pipeline-v0.3.json` and node manifest schema to `node-v0.2.json`; node `params` blocks become live (for `color.scene_linear_to_display.target`, `color.3d_lut.lut_path`, `denoise.bm3d.sigma_override`).
- `fusion.hdr_plus_derivative` lands as a registered passthrough placeholder so the editor (P3) and tests can see the node; true burst processing waits for P4 multi-frame `BatchedBuffer`.
- Per-node golden EXR fixtures land for every new node; RawTherapee 5.10 is installed on the development machine so reference goldens come from RT where it can express the algorithm, and from cpipe self-references elsewhere.
- HDR HEIF (PQ) output carries both CICP `(9, 16, 9)` and a handwritten ICC v4.4 (`lutAtoBfloatType`) profile.
- 4×4 per-Bayer-channel GainMap support resolves [Architecture §17 Q6](architecture.md#17-open-questions).

When P2 is done, the project is tagged `v0.3` and Phase 3 begins.

---

## 1. Objective

A working `cpipe run input.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o out.heif` (SDR) and a parallel `... -p examples/pipelines/full-classic-pipeline-hdr.cpipe.json -o out-hdr.heif` (HDR PQ) for both the P1 cropped Pixel 8 Pro Bayer DNG fixture and a Pixel 8 Pro 50 MP Quad Bayer Pro-mode DNG fixture, executed through the full 19-node classic ISP DAG, end-to-end on a Linux x86_64 + NVIDIA RTX + Vulkan 1.3 developer machine.

**Success looks like:**

- `examples/pipelines/full-classic-pipeline.cpipe.json` describes the 14-node SDR ISP chain (P1 carries + P2 new) plus `color.scene_linear_to_display{target: sRGB}` plus `output.heif_sdr`; runs against `tests/corpus/pixel8pro.dng` (P1 fixture) and against `tests/corpus/pixel8pro-qbc.dng` (new P2 50 MP Quad Bayer fixture) and emits viewable HEIF in both cases.
- `examples/pipelines/full-classic-pipeline-hdr.cpipe.json` swaps the last two nodes for `color.scene_linear_to_display{target: BT2020-PQ}` + `output.heif_hdr_pq` and emits an HDR HEIF that a PQ-aware viewer renders meaningfully on an HDR display (subjective at P2; recorded gate at P5).
- Per-node EXR golden fixtures (≥ 18 net new — three demosaic, one quad-bayer remosaic, two lens (gainmap + opcode_list_3), one wb_auto, four tone, three denoise, one sharpen, one 3D-LUT, one scene_linear_to_display) plus the upgraded `wb.dual_illuminant` golden pass `ctest -L golden` with PSNR ≥ 40 dB. Reference images come from RawTherapee 5.10 where it can express the algorithm (bilinear / RCD / AMaZE / WB dual / 3×3 matrix / USM); from IPOL / scikit-image / paper-trace reference outputs for BM3D / Mertens / wavelet-BayesShrink / OpcodeList; and from cpipe self-reference for everything else, with the source noted in `tests/golden/<node-id>/reference.md`.
- Integration smoke `test_full_classic_pipeline_dng_to_heif_sdr` re-decodes the produced SDR HEIF, color-converts back to linear Rec.2020 D65 EXR via OCIO inverse, and asserts PSNR ≥ 37 dB against a RawTherapee 5.10 same-parameter reference. Integration smoke `test_full_classic_pipeline_dng_to_heif_hdr` re-decodes the HDR HEIF, color-converts back to linear Rec.2020 D65 via OCIO PQ-inverse, and asserts PSNR ≥ 37 dB against the cpipe pre-HEIF Rec.2020-PQ-display reference (self-reference per [PD-70](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) carry).
- `cpipe-owned` Vulkan dispatch (`VulkanCommandBuffer::submit` + timeline wait) runs `demosaic.rcd` on the development RTX device, with Tracy GPU evidence captured for the smoke run, retiring [PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n).
- `Pipeline::load` validates the upgraded `pipeline-v0.3.json` (node `params` block), enforces the interference-graph memory plan and rejects when peak exceeds `Pipeline::set_device_memory_cap`, and auto-inserts `precision_convert` for adjacent-port mismatches; rejects `pipeline-v0.2.json` with a clear "schema version mismatch" error.

P2 explicitly does *not* deliver: HLG / UltraHDR / Apple Adaptive HDR (v1.1); the Web Editor and IQA harness and 50-image corpus and microbench (P3); AI nodes (P4); direct 4×4 Quad Bayer demosaic (v1.2); X-Trans demosaic (D12); Lensfun (v1.1); ExecuTorch / ONNX RT (P4); NVENC HEVC encoder (v1.1+); handwritten SPIR-V dispatch ops (P5); 12-bit HEVC Main12 (v2); DNG re-export (v2); vendor-specific OpcodeList opcodes (v2). See §15 for the complete OOS list.

---

## 2. Inputs

- P1 outputs (locked in `v0.2`): the 5-node walking skeleton; the `cpipe_metadata_suite_v1` + `cpipe_metadata_builder_suite_v1` ABI; LibRaw DNG ingest + first-party OpcodeList parser; the `dng_input` source plugin; `pipeline-v0.2.json` load validation; TaskFlow scheduler with linear-lifetime memory planner and validate-and-abort precision planner; the Vulkan device-plane foundation (`VulkanDevicePlane`, `VulkanBuffer`, `VulkanImage`, timeline-semaphore + fence); the SDR HEIF sink; the minimal OCIO config v0.1; Git LFS corpus/golden fixtures; Tracy spans for `Pipeline::run` / `Scheduler::dispatch_node` / `ComputeContext::submit_halide`.
- Locked design documents: [`architecture.md`](architecture.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`tech.md`](tech.md), [`roadmap.md`](roadmap.md).
- Development machine: Ubuntu 24.04+ with NVIDIA RTX-class GPU, Vulkan 1.3 driver (NVIDIA 570+), validation layers, **RawTherapee 5.10 installed for reference fixture authoring (new in P2)**.
- Git LFS installed locally and on the GitHub Actions runner (P1 carry).

---

## 3. Outputs

- Tag `v0.3` on a green main commit that satisfies §11 (DoD).
- `examples/pipelines/full-classic-pipeline.cpipe.json` (SDR) + `examples/pipelines/full-classic-pipeline-hdr.cpipe.json` (HDR PQ) reference graphs.
- `tests/corpus/pixel8pro-qbc.dng` 50 MP Pixel 8 Pro Pro-mode 4×4 Quad Bayer fixture (cropped, CC0 / CC-BY; LFS).
- EXR golden fixtures under `tests/golden/<node-id>/` for every new P2 node + upgraded WB (LFS).
- New native targets: `cpipe-runtime` grows `VulkanCommandBuffer` + GPU dispatch + interference-graph `MemoryPlanner` + auto-insert `PrecisionPlanner` + `OcioVulkanProcessor`; `cpipe-builtin-nodes` adds 16 new node binaries (3 demosaic incl. quad-remosaic - 1 because bilinear carries; 1 wb auto; 4 tone; 3 denoise; 1 sharpen; 1 3d_lut; 1 lens.shading_gainmap; 1 lens.dng_opcode_list_3; 1 color.scene_linear_to_display; 1 fusion.hdr_plus_derivative placeholder; 1 output.heif_hdr_pq) and refactors 6 existing nodes (linearize / blacklevel / wb dual / colormatrix → Halide AOT via `submit_halide_with_params`; output.heif_sdr → pure encoder; demosaic.bilinear keeps its multi-target build).
- New schemas: `schemas/pipeline-v0.3.json`, `schemas/node-v0.2.json`.
- New bundled OCIO config: `share/cpipe/ocio/v0.2/config.ocio` (3 → 4 colorspaces; 1 → 4 displays; 2 looks).

---

## 4. Phase Decisions (P2-PD-N)

P2-specific decisions, locked from the planning round on 2026-05-15. PD numbering restarts at `P2-PD-1` phase-local. Cross-references `RD-N / D-N / B-N / P-N` stay global; cross-phase references write `P1-PD-NN` / `P0-PD-NN`.

| ID         | Decision                                | Value |
|------------|------------------------------------------|-------|
| P2-PD-1    | Phase doc structure                     | Mirrors [`phase-01-walking-skeleton.md`](phase-01-walking-skeleton.md) (Objective / Inputs / Outputs / PD table / Architecture Decisions / Repo Layout / Task List / Notes / Tests / Risks / DoD / What Shipped / Deps / OOS / See Also) overlaid with the `planning-and-task-breakdown` template (Tasks-with-checkpoints). Language: English ([RD-28](roadmap.md#1-decision-quick-reference)). |
| P2-PD-2    | PD numbering                            | Phase-local `P2-PD-N`. Cross-phase references use the `P1-PD-NN` / `P0-PD-NN` form (carries P1-PD-1 convention). |
| P2-PD-3    | Branch / PR policy                      | One PR per T-task (CLAUDE.md §Workflow). Strictly sequential on `main`; cross-T parallelism is not allowed because the planner / suite changes (T1–T4) are pre-requisites of every node PR (T5 onward). Carries [P1-PD-2](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). |
| P2-PD-4    | 24-h release bake                       | Continued waiver per [P1-PD-4](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [P0-PD-37](phase-00-foundation.md): `v0.3` ships when latest `main` CI is green on the release-candidate commit, plus pushed tag and GitHub Release. |
| P2-PD-5    | Compute-suite evolution                 | `cpipe_compute_suite_v1` grows a `submit_halide_with_params(generator_name, halide_buffer_t* const* in_images, size_t n_in_images, halide_buffer_t* const* out_images, size_t n_out_images, const void* param_blob, size_t param_blob_size, ...)` function pointer at the **tail** of the suite struct. `submit_halide` (image-only, P1) keeps working — P0/P1 plugins see the older suite. `CPIPE_ABI_MINOR` rises from 2 to 3; the host advertises the new function via the suite size. No new suite name. |
| P2-PD-6    | OCIO compute helper                     | Same compute suite gains `submit_ocio_processor(processor_handle, in_image, out_image, ...)` at the tail, used by `color.scene_linear_to_display`. OCIO `ConstGPUProcessorRcPtr` is wrapped by an opaque host-side handle returned from `host->get_ocio_processor(config_path, src_cs, dst_cs)` (new function pointer on `cpipe_host_v1` at the tail). |
| P2-PD-7    | Vulkan dispatch policy                  | Original target: retire [P1-PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) by having `cpipe-runtime` own a `VulkanCommandBuffer` wrapper + per-frame `vkAllocateCommandBuffers` + timeline-semaphore wait, then bind Halide AOT SPIR-V into a `VkShaderModule` + `VkComputePipeline` and record dispatches through `VulkanCommandBuffer::record_halide_kernel(...)`. P2-PD-59 narrows the T2 implementation: cpipe-owned generic SPIR-V dispatch lands, but direct Halide AOT demosaic handoff remains a carry item because Halide v21 exposes no stable no-host-runtime descriptor handoff. |
| P2-PD-8    | Tracy GPU spans                         | Five new spans added (P1 carries three CPU spans): `VulkanCommandBuffer::submit`, `VulkanDevicePlane::wait_timeline`, `OcioVulkanProcessor::compute_pass`, `MemoryPlanner::plan_graph_coloring`, `PrecisionPlanner::auto_insert`. `CPIPE_ENABLE_TRACY` policy unchanged from [P1-PD-9](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). |
| P2-PD-9    | Memory planner upgrade                  | Linear-lifetime is replaced by interference-graph coloring per [Architecture §5.2](architecture.md#5-pipeline-lifecycle) + [Research 03 §5](research/03-heterogeneous-scheduler.md). Algorithm: topo-order liveness analysis → build interference graph (one node per live buffer; edge when lifetimes overlap) → greedy coloring → emit `buffer_id → physical_slot` mapping for VMA. Peak-vs-cap pre-check remains at `Pipeline::load`. The diamond DAG test exercises the new planner. |
| P2-PD-10   | Precision planner upgrade               | Validate-and-abort grows auto-insertion: when `producer.precision != consumer.precision`, `Pipeline::load` splices an implicit `com.cpipe.precision_convert` node (built-in, P2 new). The inserted node is visible in scheduler diagnostics but not in `pipeline-v0.3.json`. `CPIPE_BAD_PRECISION` only fires when no conversion path exists (e.g. `R16_UINT` ↔ `R16G16B16A16_SFLOAT` without explicit demosaic). |
| P2-PD-11   | Pipeline schema bump                    | `schemas/pipeline-v0.3.json`. Per-node `params` block becomes live (was required-empty in P1 PD-24). Schema additions: `target` enum on `color.scene_linear_to_display`; `lut_path` string on `color.3d_lut`; `sigma_override` number on `denoise.bm3d`. `pipeline-v0.2.json` is rejected at load with "schema version mismatch"; the four P1 fixtures + the new full-pipeline example all use v0.3. |
| P2-PD-12   | Node manifest schema bump               | `schemas/node-v0.2.json`. Adds a `params` array (each entry: `{ name, type ∈ enum/number/string/array, enum_values?, range?, default? }`). Node-v0.1 manifests still load (forward-compat). Manifest-driven editor form (P3) consumes the params block. |
| P2-PD-13   | Reverting [P1-PD-24]                    | "All ISP nodes have empty params lists" is replaced by "P2 nodes declare their needed params; others stay empty". The three live-param nodes (`color.scene_linear_to_display`, `color.3d_lut`, `denoise.bm3d`) author full param-schema entries; the rest stay empty. |
| P2-PD-14   | Demosaic family scope                   | Three nodes: `com.cpipe.demosaic.rcd` (default), `com.cpipe.demosaic.amaze` (low-ISO alt), `com.cpipe.demosaic.bilinear` (P1 carry — kept). RCD re-implemented from Sanz Rodríguez & Bayón, "A Fast Algorithm for the Demosaicing Problem Concerning the Bayer Pattern", *Open Signal Processing Journal* 6:1, 2014 (open-access). AMaZE re-implemented from Emil Martinec's public algorithm description; the GPLv3 `rtengine/amaze_demosaic_RT.cc` is **not consulted**. |
| P2-PD-15   | Quad Bayer remosaic                     | Standalone node `com.cpipe.demosaic.quad_bayer_remosaic` consumes a `R16_UINT Image2D` 4×4 QBC and produces `R16_UINT Image2D` 2×2 Bayer at the same pixel dimensions (pixel binning is **not** part of remosaic). Algorithm: 4×4 macro-block edge-aware gradient blend per Sony QBC whitepaper + MathWorks File Exchange #116085 BSD reference. The P1 `DngReader` CFA gate (P1-PD-14) is widened to accept 4×4 Bayer when the downstream pipeline includes `com.cpipe.demosaic.quad_bayer_remosaic` upstream of every demosaic node. |
| P2-PD-16   | OpcodeList2 GainMap node                | New standalone node `com.cpipe.lens.shading_gainmap` consumes the `BufferMetadata.ext_blobs["com.cpipe.dng.opcode_list_2_bytes"]` raw bytes (P1 preserved), interprets the GainMap opcode, and applies multiplicative gain per Bayer channel. Position in the pipeline: between `linearize` and `blacklevel` per DNG spec semantics. Halide AOT, parameter-buffer driven (the parsed GainMap planes are passed via `submit_halide_with_params`). |
| P2-PD-17   | OpcodeList3 dispatcher                  | Single node `com.cpipe.lens.dng_opcode_list_3` implements **five** opcodes: `WarpRectilinear`, `FixVignetteRadial`, `FixBadPixelsConstant`, `FixBadPixelsList`, `TrimBounds`. GainMap is **not** in this node — it lives in `lens.shading_gainmap` (P2-PD-16). Position: after `demosaic` per DNG spec. Halide AOT; bicubic resample for `WarpRectilinear`. |
| P2-PD-18   | Quad Bayer GainMap (Q6 resolution)      | Resolves [Architecture §17 Q6](architecture.md#17-open-questions). When the DNG declares a 4×4 CFA and the GainMap is multi-plane (per-Bayer-channel × 4 planes), `lens.shading_gainmap` applies each plane to its corresponding sub-CFA before remosaic (i.e., `lens.shading_gainmap` runs at the 4×4 stage; remosaic follows). The 2×2 CFA path applies the 4-plane GainMap by Bayer position (RGGB / BGGR / GRBG / GBRG). Q6 is then logged as Resolved in P2-PD-18 in [architecture.md §17](architecture.md#17-open-questions). |
| P2-PD-19   | wb.dual_illuminant upgrade              | The P1 minimal `invert(AsShotNeutral)` is replaced in-place on the same node ID `com.cpipe.wb.dual_illuminant`. New behavior: estimate scene CCT from `AsShotNeutral` via 1-D Brent search over the dual `ColorMatrix1/2`, then linearly interpolate `ForwardMatrix1/2` at reciprocal-CCT weight, then emit camera-WB diag + the interpolated camera→XYZ-D50 matrix (the interpolated matrix flows to `colormatrix.dng_to_working` through metadata). Halide AOT via `submit_halide_with_params`. Manifest is binary-stable across the upgrade (P1 plugins do not depend on the math). Reference: Adobe DNG 1.7 Spec §6.4.3 + Research 13 §3.6. |
| P2-PD-20   | wb.greyworld_auto                       | New helper node `com.cpipe.wb.greyworld_auto`. Used in pipelines where `AsShotNeutral` is missing (rare on modern DNG); estimates a gray-world neutral and emits the same metadata surface as `wb.dual_illuminant`. CPU plugin loop (host engine); upgrades to Halide AOT in P5 if microbench justifies. |
| P2-PD-21   | colormatrix.dng_to_working upgrade      | The P1 hand-coded ColorMatrix1 chain is upgraded to consume the **interpolated** camera→XYZ-D50 matrix that `wb.dual_illuminant` (P2-PD-19) writes through metadata. Bradford D50→D65 and XYZ-D65→Rec.2020 D65 stay as constant matrices baked into the Halide generator. Halide AOT via `submit_halide_with_params`. Same node ID. |
| P2-PD-22   | Tone family scope                       | Four nodes: `com.cpipe.tone.filmic_rgb` (re-impl from Aurelien Pierre 2018 + darktable filmic-RGB module manual), `com.cpipe.tone.mertens_local` (re-impl from IPOL 2018, BSD; SDR only — HDR-pyramid variant deferred to v1.1 per Research 07 OQ 6), `com.cpipe.tone.aces_filmic` (Narkowicz 2016 fit, Apache 2 compat), `com.cpipe.tone.reinhard` (Reinhard 2002 one-liner; debug). All Halide AOT. All have empty `params` in P2; editor exposes them in P3. |
| P2-PD-23   | Denoise family scope                    | Three nodes: `com.cpipe.denoise.bm3d` (re-impl from Dabov 2007 + Mäkinen 2020 reference, BSD), `com.cpipe.denoise.guided_filter` (He 2010 paper), `com.cpipe.denoise.wavelet_bayes_shrink` (re-impl from Chang 2000 + scikit-image BSD3 reference). BM3D is Halide AOT and consumes `NoiseProfile` β/λ per-channel via metadata for sigma map. BM3D may declare `sigma_override` param (P2-PD-13). |
| P2-PD-24   | Sharpen node                            | Single node `com.cpipe.sharpen.edge_aware_usm` per He 2010 / 2013 ("Fast Guided Filter"). Halide AOT. Empty params in P2. |
| P2-PD-25   | color.3d_lut + LUT loaders              | Node `com.cpipe.color.3d_lut` ships a CPU-side loader for `.cube` (Adobe / Resolve) and `.spi3d` (OCIO). Default interpolation: tetrahedral (Research 07 OQ 7 tentatively recommends tetrahedral, P2 confirms). Trilinear available via a future param (P2 hardcodes tetrahedral). Halide AOT for the apply path. `lut_path` param (string) is mandatory. |
| P2-PD-26   | color.scene_linear_to_display           | New node `com.cpipe.color.scene_linear_to_display` with `target` param. P2 supports `sRGB` and `BT2020-PQ` enum values; `DisplayP3` and `BT2020-HLG` are declared in the param schema but return `CPIPE_UNSUPPORTED` from `process()` until v1.1. The node calls `cpipe_compute_suite_v1::submit_ocio_processor` (P2-PD-6) with the configured OCIO processor handle. Input precision: `R16G16B16A16_SFLOAT`; output precision: `R8G8B8A8_UNORM` for `sRGB`, `R16G16B16A16_UNORM` for `BT2020-PQ` (10-bit data in the top 10 bits). |
| P2-PD-27   | output.heif_sdr refactor                | `com.cpipe.output.heif_sdr` is refactored to be a pure encoder: input precision changes from `R16G16B16A16_SFLOAT` (Rec.2020 D65 scene-linear) to `R8G8B8A8_UNORM` (sRGB SDR, alpha discarded). The OCIO scene→sRGB transform retired from this node — `color.scene_linear_to_display{target: sRGB}` now provides it. CICP `(1, 13, 1)` + sRGB ICC v4 unchanged. Existing `pipeline-v0.2.json` fixtures that target the P1 `output.heif_sdr` input contract are migrated to `pipeline-v0.3.json` in T20. |
| P2-PD-28   | output.heif_hdr_pq                      | New node `com.cpipe.output.heif_hdr_pq`. Input: `R16G16B16A16_UNORM` (BT.2100-PQ 10-bit, top-10-bits, alpha discarded). Encoder: kvazaar 2.3 Main10 (`--preset=medium`, 10-bit Main, 4:2:0 chroma, QP default). HEIF container: libheif 1.20.1; CICP `(9, 16, 9)`, `full_range=1`. `mdcv` static default (BT.2020 primaries, D65, 0.005–1000 nits); `clli` computed at encode time from the pre-PQ linear pixels (`MaxCLL` = max per-pixel nits, `MaxFALL` = mean per-pixel nits). Also embeds a handwritten ICC v4.4 profile carrying a `lutAtoBfloatType` BT.2020-PQ description (P2-PD-29). |
| P2-PD-29   | HDR HEIF ICC v4.4 lutAtoBfloatType      | A handwritten ICC v4.4 builder in `cpipe::color::IccV4_4Writer` (≈ 250 LOC) emits a profile with `colorSpace = RGB`, `pcs = XYZ`, `cicp` tag set to `(9, 16, 9, full_range=1)`, and a `lutAtoBfloatType` A2B0 tag that carries the BT.2020 primaries → PCS XYZ matrix and the PQ EOTF as an FP16 1D-LUT. Decision rationale: lcms 2.16 does not honor the `cicp` tag in transforms (Research 13 §3.10), but most modern readers (iOS / macOS / Photoshop / Lightroom) read either CICP or ICC; cpipe writes both so the reader picks. |
| P2-PD-30   | OCIO config v0.2                        | `share/cpipe/ocio/v0.2/config.ocio` with four colorspaces (`raw_camera`, `scene_linear_rec2020`, `output_srgb`, `output_pq_rec2020`), four displays (`sRGB`, `Display P3`, `Rec.2020 PQ`, `Rec.2020 HLG`), and two ship-by-default views per display (`Untouched` + `Standard SDR` / `Standard HDR` looks). The looks are simple `FileTransform` references to FP16 1D-LUTs bundled at `share/cpipe/ocio/v0.2/luts/`. `CPIPE_OCIO_CONFIG` env override unchanged. `share/cpipe/ocio/v0.1/` retired. |
| P2-PD-31   | fusion.hdr_plus_derivative placeholder  | Registered node `com.cpipe.fusion.hdr_plus_derivative` with manifest ports `inputs=[ref: R16_UINT Image2D, frame: R16_UINT Image2D]` and `outputs=[fused: R16_UINT Image2D]`. `process()` returns `outputs[0] = inputs[0]` (the ref frame) and `out_metadata->add_applied_step("burst_fusion_stub")`. Pipeline JSON may bind both ports to the same source — the editor sees the node, the runtime treats it as a passthrough. P4 replaces `process()` with true HDR+ derivative align/merge once `BatchedBuffer` lands. |
| P2-PD-32   | precision_convert built-in              | New built-in node `com.cpipe.precision_convert` registered by `cpipe-builtin-nodes`. Used implicitly by the precision planner (P2-PD-10); not addressable from `pipeline-v0.3.json`. Halide AOT; supports `R16_UINT → R32_SFLOAT`, `R32_SFLOAT → R16G16B16A16_SFLOAT`, `R16G16B16A16_SFLOAT → R8G8B8A8_UNORM`, `R16G16B16A16_SFLOAT → R16G16B16A16_UNORM` (PQ scaling). |
| P2-PD-33   | Reference pipeline node order           | Locked: `dng_input → linearize → lens.shading_gainmap → blacklevel → demosaic.rcd → lens.dng_opcode_list_3 → wb.dual_illuminant → colormatrix.dng_to_working → denoise.bm3d → denoise.wavelet_bayes_shrink → tone.filmic_rgb → color.3d_lut → sharpen.edge_aware_usm → color.scene_linear_to_display{target} → output.heif_{sdr or hdr_pq}`. `lens.shading_gainmap` precedes `blacklevel` (it operates on linearized signal, per DNG spec). 14 nodes for the SDR pipeline, 14 nodes for the HDR pipeline (only the last two differ by `target` and sink). |
| P2-PD-34   | Reference goldens — RT 5.10             | RawTherapee 5.10 is installed on the development machine (apt-get / AppImage) and produces reference fixtures for `demosaic.bilinear`, `demosaic.rcd`, `demosaic.amaze`, `wb.dual_illuminant` (full upgrade), `wb.greyworld_auto`, `colormatrix.dng_to_working` (upgrade), `sharpen.edge_aware_usm`. PSNR threshold ≥ 40 dB. RawTherapee invocation: scripted via `tools/golden/rt_render.sh` checked into the repo; CI does not install RT (carries P1-PD-44 LFS strategy). |
| P2-PD-35   | Reference goldens — IPOL / scikit-image | `denoise.bm3d` goldens reference IPOL 2017 BM3D Mäkinen FP32 output; `denoise.wavelet_bayes_shrink` references the scikit-image BSD3 implementation; `tone.mertens_local` references IPOL 2018 exposure-fusion; `tone.aces_filmic` references the Narkowicz fit applied via NumPy; `tone.reinhard` references the closed-form one-liner; `lens.dng_opcode_list_3` references colour-hdri's Python DNG model (BSD-3). Reference scripts live under `tools/golden/`. PSNR ≥ 40 dB. |
| P2-PD-36   | Reference goldens — cpipe self          | `lens.shading_gainmap` (no public reference applies the per-Bayer multi-plane interpretation); `color.3d_lut` (synthetic LUT tetrahedral interp); `tone.filmic_rgb` (Pierre's blog math is reproducible but no reference impl emits FP32 EXR cleanly — cpipe self-reference); `color.scene_linear_to_display` (OCIO produces our own reference); `fusion.hdr_plus_derivative` (passthrough); `demosaic.quad_bayer_remosaic` (Sony spec but no FP32 EXR reference). PSNR ≥ 40 dB against cpipe self EXR; `reference.md` notes the source. |
| P2-PD-37   | Quad Bayer fixture                      | `tests/corpus/pixel8pro-qbc.dng` — cropped 3840×2880 (preserves 4×4 macro-block alignment) from a Pixel 8 Pro 50 MP Pro-mode QBC DNG sourced under CC0 / CC-BY (raw.pixls.us if a 50 MP variant is available; otherwise Imaging Resource / dpreview QBC sample). Provenance committed to `tests/corpus/pixel8pro-qbc.source.md`. Source SHA-256 + cropped SHA-256 captured. |
| P2-PD-38   | RawTherapee install policy              | RawTherapee 5.10 installed on the development machine for golden authoring (P2-PD-34). CI does **not** install RawTherapee — goldens are checked in via Git LFS once authored locally. CI verifies the goldens are byte-stable, not regenerated. |
| P2-PD-39   | CLI surface                             | P2 CLI surface remains `cpipe run` only (carries [P1-PD-45](phase-01-walking-skeleton.md#4-phase-decisions-pd-n)). `cpipe info / serve / bench / iqa / model` remain unshipped; the editor's registry-walk endpoint comes online in P3. |
| P2-PD-40   | Integration smoke shape                 | Two new integration smokes alongside the P1 `test_min_pipeline_dng_to_heif`: `test_full_classic_pipeline_dng_to_heif_sdr` (Pixel 8 Pro Bayer DNG → full SDR HEIF, re-decode + OCIO inverse, PSNR ≥ 37 dB vs RT 5.10 reference rendered with matching params); `test_full_classic_pipeline_dng_to_heif_hdr` (Pixel 8 Pro Bayer DNG → full HDR-PQ HEIF, re-decode + OCIO PQ inverse, PSNR ≥ 37 dB vs cpipe pre-HEIF Rec.2020-PQ-display reference). The P1 min-pipeline smoke continues to gate. |
| P2-PD-41   | LFS additions                           | Per [P1-PD-3](phase-01-walking-skeleton.md#4-phase-decisions-pd-n), `*.exr` and `*.dng` already on LFS. P2 adds `*.icc` (HDR ICC profile bytes) and `*.cube` (test 3D LUTs) to the LFS filters in `.gitattributes`. P2 does **not** add `*.heif` to LFS — generated test artifacts stay in `/tmp` per the existing CI convention. |
| P2-PD-42   | CI Vulkan policy                        | Carries [P1-PD-73](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). GitHub-hosted runners have no Vulkan ICD; `VulkanDevicePlane::create()` returns `CPIPE_UNSUPPORTED` on CI. Tests labeled `vulkan` are conditional on `CPIPE_VULKAN_AVAILABLE`. The development RTX machine is the P2 Vulkan correctness gate. T2's Tracy GPU capture is a local artifact, not a CI artifact. |
| P2-PD-43   | LSan policy                             | Carries [P1-PD-52](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). New Vulkan / OCIO-Vulkan tests run with `ASAN_OPTIONS=detect_leaks=0`. AddressSanitizer + UBSan remain enabled. |
| P2-PD-44   | clang-tidy policy                       | Carries [P1-PD-74](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). New P2 functions are written under the strict cognitive-complexity threshold; targeted suppressions added only when a refactor would distort the algorithm structure (BM3D pyramid, OpcodeList3 dispatcher). |
| P2-PD-45   | Risk register                           | Phase-local risks tracked in §10 (`P2-R1` … `P2-R7`); inherited risks from [Research 00 §7](research/00-summary.md#7-risk-register) cited but not restated. |
| P2-PD-46   | What Shipped / What Slipped flow        | Phase doc §12 + [`roadmap.md` §5](roadmap.md#5-phase-2--classic-nodes--hdr-tag-v03) + [`README.md`](../README.md) "Current Status" updated in the closing PR that pushes `v0.3`. |
| P2-PD-47   | Reverting [P1-PD-56] and [P1-PD-60]     | The "engine: Host" classification on `linearize.dng_lut`, `blacklevel.dng_levels`, `wb.dual_illuminant`, `colormatrix.dng_to_working` is reverted to `engine: Halide` (CPU AOT, with parameter buffers via P2-PD-5). Migrations land together in T1; subsequent math upgrades follow in T11 (`wb.dual_illuminant`, [P2-PD-19](#4-phase-decisions-p2-pd-n)) and T12 (`colormatrix.dng_to_working` parameter source upgrade, [P2-PD-21](#4-phase-decisions-p2-pd-n)). |
| P2-PD-48   | Reverting [P1-PD-59]                    | The "RGGB only" gate on `demosaic.bilinear` is widened to accept BGGR / GRBG / GBRG via a CFA-pattern uniform passed through `submit_halide_with_params`. RCD / AMaZE inherit four-pattern support from day one. |
| P2-PD-49   | Reverting [P1-PD-22] (replaces P2-PD-19) | The minimal-WB stand-in is replaced in place; same node ID, same manifest, new math. Phase tag carries the breaking-math change in the release notes. |
| P2-PD-50   | Halide schedule discipline              | Each new Halide generator (`rcd_generator`, `amaze_generator`, `quad_bayer_remosaic_generator`, `bm3d_step1_generator`, `bm3d_step2_generator`, `wavelet_bayes_shrink_generator`, `guided_filter_generator`, `filmic_rgb_generator`, `mertens_local_generator`, `aces_filmic_generator`, `reinhard_generator`, `sharpen_usm_generator`, `lens_shading_gainmap_generator`, `opcode_list_3_generator`, `lut3d_apply_generator`, `precision_convert_generator`) ships **CPU AOT only** in P2. The `host-vulkan` multi-target is built only for `demosaic.bilinear` (P1 carry) and `demosaic.rcd` (P2 new — see P2-PD-51). Other Halide GPU paths are P3+ work. |
| P2-PD-51   | demosaic.rcd Vulkan AOT                 | `demosaic.rcd` builds both `${Halide_HOST_TARGET}` (CPU) and `${Halide_HOST_TARGET}-vulkan-vk_float16` (carries P1-PD-58 FP16 target feature). This was planned as the P2 proof of cpipe-owned Vulkan dispatch (P2-PD-7); P2-PD-59 carries the direct Halide AOT command-buffer handoff until a supported descriptor-handoff path exists. |
| P2-PD-52   | clang-tidy / clang-format adherence     | Carries [P1-PD-22 R / RD-22]. All P2 source obeys `clang-format` + `clang-tidy` + ASAN/UBSAN Debug gates. |
| P2-PD-53   | Halide custom_par_for redirect          | Carries [P1 architectural note §8](phase-01-walking-skeleton.md#8-architecture-notes-p1-specific). `halide_set_custom_do_par_for` continues to delegate Halide CPU work to the project-wide `tf::Executor`; P2 verifies the redirect under interference-graph memory plan + auto-precision-insert. |
| P2-PD-54   | NodePort role validation                | Carries [P1-PD-61](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). `Pipeline::load` continues to enforce `producer.color.output_role` vs `consumer.color.input_role`; the upgraded `wb.dual_illuminant` and `colormatrix.dng_to_working` keep the role semantics; new nodes declare roles per the canonical ordering. |
| P2-PD-55   | License audit cadence                   | Carries [RD-23](roadmap.md#1-decision-quick-reference): manual at PR review. T9 (HDR PQ output) reviewer verifies `ldd $cpipe-cli` has no `libx265`; T1 reviewer verifies vcpkg manifest does not pull in GPL deps with the new compute-suite extension. |
| P2-PD-56   | Halide AOT filter self-registration     | T0 retires the P0/P1 caller-side `ComputeContext::register_halide_filter(...)` pattern. Root cause: [`phase-00-foundation.md` §7 architecture note](phase-00-foundation.md) ("a string-keyed map populated at startup with each AOT symbol the runtime knows about") did not lock a *how*; the P0 skeleton (commit `303055d`, `feat: add p0 runtime dispatch skeleton`) chose caller-side imperative registration because P0 had a single AOT (`passthrough_copy`). P1 added one more (`demosaic_bilinear`) and the four host-loop nodes ([P1-PD-56](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [P1-PD-60](phase-01-walking-skeleton.md#4-phase-decisions-pd-n)) deliberately stayed off Halide AOT, masking the smell. [P2-PD-47](#4-phase-decisions-p2-pd-n)'s revert moves AOT count from 2 → 6 in T1 alone and Phase 2 will add ~16 more, making the imperative pattern untenable. **New mechanism:** every node's `*_dispatch.cpp` (or `passthrough` / `demosaic.bilinear` for the legacy two) uses `CPIPE_REGISTER_HALIDE_FILTER(aot_id, &entry)` or `CPIPE_REGISTER_HALIDE_PARAM_FILTER(aot_id, &adapter)` to push itself into a `cpipe::runtime::HalideFilterRegistry` (thread-safe global, populated at static-init time, same idiom as `CPIPE_REGISTER_NODE`). `ComputeContext` pulls from the registry on construction. Host call sites (`Pipeline.cpp` and every test) **must not** invoke `register_halide_*` directly. Legacy imperative `register_halide_filter` / `register_halide_param_filter` methods stay on `ComputeContext` as test-injection seams only (e.g. one-shot in-test dispatchers like `test_scale_by_param`) but are no longer used by production nodes. |
| P2-PD-57   | Compute-suite tail-extension probe      | `cpipe_compute_suite_v1` and `cpipe_host_v1` grow at the tail (P2-PD-5 / P2-PD-6); the host always populates the full struct; plugins compiled against the v0.2 header read only the original entries via layout-stable tail-append. New plugins probe `host->abi_minor >= 3` (one-line guard) before touching `submit_halide_with_params` / `submit_ocio_processor` / `get_ocio_processor`. There is no separate size field on the suite — the ABI minor is the version cursor. |
| P2-PD-58   | OCIO CPU fallback in T1                 | `submit_ocio_processor` ships with a CPU-processor fallback in T1 (FP32 / FP16 RGBA via `OCIO::PackedImageDesc`). The Vulkan-backed path (P2-PD-6 / `OcioVulkanProcessor`) lands in T19 once T2 (cpipe-owned Vulkan dispatch) is available. T1 satisfies the acceptance bullet "`get_ocio_processor` returns a non-null opaque handle" and the round-trip semantics; GPU dispatch is T19's gate. The opaque handle type stays stable across the upgrade because `cpipe_ocio_processor_s` holds both a `ConstCPUProcessorRcPtr` and (in T19) a Vulkan companion. |
| P2-PD-59   | Halide Vulkan AOT handoff scope         | T2 lands the cpipe-owned Vulkan dispatch primitive (`VulkanCommandBuffer` records `vkCmdBindPipeline` / `vkCmdBindDescriptorSets` / `vkCmdDispatch`, submits to the cpipe-owned queue, and signals a timeline semaphore). Local Halide v21 evidence shows `add_halide_library()` already appends `-no_runtime`, but the generated Vulkan object still calls the overrideable Halide entry points (`halide_vulkan_initialize_kernels`, `halide_vulkan_run`, `halide_vulkan_finalize_kernels`, `halide_vulkan_device_interface`) and the shipped CMake/docs expose no `HL_VULKAN_NO_HOST_RUNTIME` switch or stable AOT descriptor handoff for cpipe to bind `demosaic.bilinear` SPIR-V directly without reimplementing Halide's private Vulkan runtime sidecar decoder. Therefore T2 does **not** claim that `demosaic.bilinear` has moved to cpipe-owned command-buffer dispatch; that narrower Halide handoff remains a carry item. OCIO Vulkan in T19 is still unblocked because it supplies its own SPIR-V + descriptor layout and can use `VulkanCommandBuffer` directly. |
| P2-PD-60   | `precision_convert` schema timing       | T4 lands before T5's `pipeline-v0.3.json`, so the "not addressable from pipeline-v0.3.json" rule is implemented as a loader-level reserved-node rejection in the current v0.2 loader. T5 carries that same reserved-node rule forward when it introduces `pipeline-v0.3.json`; user-authored `com.cpipe.precision_convert` fails with `CPIPE_BAD_INDEX`, while planner-inserted instances remain internal. |
| P2-PD-61   | T6 RCD fixture / Vulkan evidence        | T6 ships `com.cpipe.demosaic.rcd` as a Halide CPU + `${Halide_HOST_TARGET}-vulkan-vk_float16` AOT multi-target and keeps direct Halide command-buffer ownership under the P2-PD-59 carry item. The local development host has no RawTherapee 5.10 binary on `PATH` and no checked-in `tools/golden/rt_render.sh` yet, so `tests/golden/demosaic.rcd/{in,out}.exr` is a deterministic cpipe self-reference generated by `gen_golden_isp_nodes`; `tests/golden/demosaic.rcd/reference.md` records the source. RT-derived RCD fixture refresh remains a later golden-authoring task, not a T6 code gate. |

---

## 5. Architecture Decisions (cross-cutting)

The PD table locks specific values; this short narrative explains the *why* for the cross-cutting choices.

- **Compute suite carries parameter buffers.** The P1 plugin shim ran `linearize`, `blacklevel`, `wb`, `colormatrix` as CPU host loops because `cpipe_compute_suite_v1::submit_halide` only accepted image buffers ([P1-PD-56](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [P1-PD-60](phase-01-walking-skeleton.md#4-phase-decisions-pd-n)). P2-PD-5 grows the suite struct in place — tail-append `submit_halide_with_params` and `submit_ocio_processor` (P2-PD-6) — bumps `CPIPE_ABI_MINOR` from 2 to 3, and leaves the v1 entries for P0/P1 plugins. The `cpipe::sdk` C++ facade picks the new entry by default. This keeps the manifest `engine: Halide` declaration meaningful and feeds the Halide multi-target story.

- **cpipe owns Vulkan dispatch.** [P1-PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) accepted that Halide's internal Vulkan runtime drove the `demosaic.bilinear` GPU run. T2 lands the cpipe-owned `VulkanCommandBuffer` primitive: cpipe binds caller-supplied SPIR-V into a `VkShaderModule` + `VkComputePipeline`, records `vkCmdDispatch`, signals/awaits a timeline semaphore, and feeds Tracy span points. P2-PD-59 keeps the Halide AOT demosaic handoff as a carry item because Halide v21 does not expose the planned no-host-runtime descriptor handoff. OCIO-Vulkan remains unblocked because T19 owns its SPIR-V and descriptor layout directly (P2-PD-6 / P2-PD-26).

- **Memory planner widens to interference-graph coloring.** Linear lifetime sufficed for the P1 linear chain. P2's 14-node DAG has fan-out / fan-in (`color.3d_lut` parallel to `tone.*` may grow with future params, `denoise.bm3d` consumes both luma and chroma in independent stages, `lens.shading_gainmap` keeps the raw buffer alive for the QBC remosaic), and tracking peak per-physical-slot requires an explicit interference graph. The implementation lives in `MemoryPlanner::plan_graph_coloring()` (P2-PD-9), runs at `Pipeline::load`, and pre-allocates VMA-backed slots before any worker spins up. The diamond DAG test (P1 carry) plus three new fan-out tests cover correctness.

- **Precision planner auto-inserts conversions.** The P1 validate-and-abort floor is brittle once a denoise path consumes 8-bit chroma or once the output sink consumes `R8G8B8A8_UNORM` (P2-PD-27). P2-PD-10 introduces a built-in `com.cpipe.precision_convert` node (P2-PD-32) that the planner can splice automatically. Diagnostics surface in spdlog (so scheduler traces show the implicit node) but the user-visible `pipeline-v0.3.json` is unchanged.

- **`color.scene_linear_to_display` is its own node.** The P1 `output.heif_sdr` bundled scene-linear → sRGB OCIO inside the sink. With HDR PQ joining (P2-PD-28) and the editor (P3) needing a per-display LiveView, P2 extracts `color.scene_linear_to_display` (P2-PD-26) with a `target` enum and lets the encoder be pure (P2-PD-27). This matches the OCIO pattern from Research 13 §3.11 (tone-map in scene-linear, then OETF/Display, then encode).

- **OpcodeList2 and OpcodeList3 are separate nodes.** OpcodeList2 (GainMap) is semantically a *post-linearize-pre-blacklevel* operation per the DNG spec — it modifies the raw signal. OpcodeList3 (lens corrections, bad-pixel patch) runs in linear RGB after demosaic. Bundling them into one dispatcher would force one node to live in two pipeline positions. P2-PD-16 + P2-PD-17 split them cleanly.

- **WB upgrade is in place (same node ID).** P2-PD-19 carries the same `com.cpipe.wb.dual_illuminant` manifest from P1; only the implementation changes. This preserves the locked roadmap §5 sub-domain `wb.dual_illuminant`. The minimal stand-in is replaced; release notes describe the math change. `colormatrix.dng_to_working` (P2-PD-21) consumes the interpolated matrix through metadata, keeping the camera→working transform a clean two-node chain.

- **Reference goldens come from three sources.** P2-PD-34 / P2-PD-35 / P2-PD-36 collectively answer the P1-PD-69 / P1-PD-70 slips: RawTherapee 5.10 for everything it can render (bilinear / RCD / AMaZE / WB / matrix / USM), IPOL or scikit-image or analytic references for everything else with a public reference implementation, and cpipe self-reference for the residual where no public reference applies. Each `tests/golden/<node-id>/reference.md` notes the source.

- **HDR ICC v4.4 is handwritten.** lcms 2.16 cannot honor the `cicp` tag (Research 13 §3.10), and a BT.2020-PQ ICC profile needs `lutAtoBfloatType` (v4.4-only). Rather than wait for lcms 2.17, P2-PD-29 ships a ~250-LOC ICC writer that emits the precise profile P2 needs. Readers that prefer CICP read CICP; readers that prefer ICC read ICC; both are correct.

---

## 6. Repository Layout (P2 end-state, additions only)

```
cpipe/
├── .gitattributes                  # P2-PD-41: *.icc / *.cube → LFS
├── examples/
│   └── pipelines/
│       ├── min-pipeline.cpipe.json (migrated to pipeline-v0.3.json)
│       ├── full-classic-pipeline.cpipe.json       # P2-PD-33 SDR
│       └── full-classic-pipeline-hdr.cpipe.json   # P2-PD-33 HDR PQ
├── include/
│   └── cpipe/
│       ├── color/
│       │   ├── IccV4_4Writer.hpp                  # P2-PD-29
│       │   ├── OcioVulkanProcessor.hpp            # P2-PD-6
│       │   └── Cube3dLutLoader.hpp                # P2-PD-25
│       ├── ingest/
│       │   └── dng_opcode/
│       │       ├── OpcodeList2.hpp                # GainMap interp (P2-PD-16)
│       │       └── OpcodeList3.hpp                # 5 opcodes (P2-PD-17)
│       └── runtime/
│           ├── VulkanCommandBuffer.hpp            # P2-PD-7
│           ├── MemoryPlanner.hpp                  # rewritten (P2-PD-9)
│           ├── PrecisionPlanner.hpp               # auto-insert (P2-PD-10)
│           └── ComputeSuiteV1Ext.hpp              # P2-PD-5/6 tail extensions
├── schemas/
│   ├── node-v0.2.json                              # P2-PD-12
│   └── pipeline-v0.3.json                          # P2-PD-11
├── share/
│   └── cpipe/
│       └── ocio/
│           └── v0.2/
│               ├── config.ocio                     # P2-PD-30
│               └── luts/                           # FP16 1D-LUT bundles
├── src/
│   └── cpipe/
│       ├── color/
│       │   ├── IccV4_4Writer.cpp
│       │   ├── OcioVulkanProcessor.cpp
│       │   ├── Cube3dLutLoader.cpp
│       │   ├── HeifWriter.cpp                      # extended for PQ Main10
│       │   └── HeifReader.cpp                      # extended for PQ decode
│       ├── ingest/
│       │   └── dng_opcode/
│       │       ├── OpcodeList2.cpp
│       │       └── OpcodeList3.cpp
│       ├── nodes/
│       │   ├── demosaic/
│       │   │   ├── demosaic_rcd*.{cpp,json,_generator.cpp}                # P2-PD-14
│       │   │   ├── demosaic_amaze*.{cpp,json,_generator.cpp}              # P2-PD-14
│       │   │   └── demosaic_quad_bayer_remosaic*.{cpp,json,_generator.cpp}  # P2-PD-15
│       │   ├── lens/
│       │   │   ├── shading_gainmap*.{cpp,json,_generator.cpp}             # P2-PD-16
│       │   │   └── dng_opcode_list_3*.{cpp,json,_generator.cpp}           # P2-PD-17
│       │   ├── wb/
│       │   │   ├── dual_illuminant*.{cpp,json,_generator.cpp}             # P2-PD-19 (in-place)
│       │   │   └── greyworld_auto*.{cpp,json}                              # P2-PD-20 (Host engine)
│       │   ├── colormatrix/
│       │   │   └── dng_to_working*.{cpp,json,_generator.cpp}              # P2-PD-21 (upgraded)
│       │   ├── denoise/
│       │   │   ├── bm3d*.{cpp,json,_generator.cpp}                         # P2-PD-23
│       │   │   ├── guided_filter*.{cpp,json,_generator.cpp}                # P2-PD-23
│       │   │   └── wavelet_bayes_shrink*.{cpp,json,_generator.cpp}         # P2-PD-23
│       │   ├── tone/
│       │   │   ├── filmic_rgb*.{cpp,json,_generator.cpp}                   # P2-PD-22
│       │   │   ├── mertens_local*.{cpp,json,_generator.cpp}                # P2-PD-22
│       │   │   ├── aces_filmic*.{cpp,json,_generator.cpp}                  # P2-PD-22
│       │   │   └── reinhard*.{cpp,json,_generator.cpp}                     # P2-PD-22
│       │   ├── sharpen/
│       │   │   └── edge_aware_usm*.{cpp,json,_generator.cpp}               # P2-PD-24
│       │   ├── color/
│       │   │   ├── lut3d*.{cpp,json,_generator.cpp}                        # P2-PD-25
│       │   │   └── scene_linear_to_display*.{cpp,json}                     # P2-PD-26 (OCIO Vulkan)
│       │   ├── fusion/
│       │   │   └── hdr_plus_derivative*.{cpp,json}                         # P2-PD-31 placeholder
│       │   ├── output/
│       │   │   ├── heif_sdr*.{cpp,json}                                    # P2-PD-27 refactor
│       │   │   └── heif_hdr_pq*.{cpp,json}                                 # P2-PD-28
│       │   └── precision/
│       │       └── precision_convert*.{cpp,json,_generator.cpp}            # P2-PD-32
│       └── runtime/
│           ├── VulkanCommandBuffer.cpp                                     # P2-PD-7
│           ├── MemoryPlanner.cpp                                           # rewritten
│           └── PrecisionPlanner.cpp                                        # rewritten
├── tests/
│   ├── corpus/
│   │   ├── pixel8pro.dng                          # P1 carry
│   │   └── pixel8pro-qbc.dng                      # P2-PD-37 (LFS)
│   ├── golden/                                    # LFS
│   │   ├── demosaic.rcd/
│   │   ├── demosaic.amaze/
│   │   ├── demosaic.quad_bayer_remosaic/
│   │   ├── lens.shading_gainmap/
│   │   ├── lens.dng_opcode_list_3/
│   │   ├── wb.dual_illuminant/                    # regenerated (P2-PD-19)
│   │   ├── wb.greyworld_auto/
│   │   ├── colormatrix.dng_to_working/            # regenerated (P2-PD-21)
│   │   ├── tone.filmic_rgb/
│   │   ├── tone.mertens_local/
│   │   ├── tone.aces_filmic/
│   │   ├── tone.reinhard/
│   │   ├── denoise.bm3d/
│   │   ├── denoise.guided_filter/
│   │   ├── denoise.wavelet_bayes_shrink/
│   │   ├── sharpen.edge_aware_usm/
│   │   ├── color.3d_lut/
│   │   └── color.scene_linear_to_display/
│   ├── integration/
│   │   ├── test_full_classic_pipeline_dng_to_heif_sdr.cpp                  # P2-PD-40
│   │   └── test_full_classic_pipeline_dng_to_heif_hdr.cpp                  # P2-PD-40
│   └── unit/
│       └── ...                                                              # see §9
├── tools/
│   └── golden/
│       ├── rt_render.sh                            # P2-PD-34
│       ├── ipol_bm3d_render.py                     # P2-PD-35
│       ├── ipol_mertens_render.py                  # P2-PD-35
│       ├── skimage_wavelet_render.py               # P2-PD-35
│       ├── narkowicz_aces_render.py                # P2-PD-35
│       └── colour_hdri_opcode3_render.py           # P2-PD-35
└── vcpkg.json                                      # no new deps in P2 (P1 carries)
```

---

## 7. Task List

Twenty-two vertical T-tasks (T0 + T1–T21). Three checkpoints. Each task lands a complete, testable slice in dependency order. Sub-task PRs are not allowed (P2-PD-3).

### Phase 2.A — Foundation (planner + compute-suite + Vulkan dispatch)

#### T0 — Halide AOT filter self-registration

**Description.** Implements [P2-PD-56](#4-phase-decisions-p2-pd-n). Adds `cpipe::runtime::HalideFilterRegistry` (thread-safe global, populated at static-init time, mirror of `CPIPE_REGISTER_NODE`); adds two macros `CPIPE_REGISTER_HALIDE_FILTER(aot_id, &entry)` and `CPIPE_REGISTER_HALIDE_PARAM_FILTER(aot_id, &adapter)`; rewires `ComputeContext` so its constructor seeds `halide_filters_` + `halide_param_filters_` from the registry. Migrates the two legacy P0/P1 imperative call sites: `passthrough_copy` gains a `passthrough_copy_dispatch.cpp` that self-registers; `demosaic.bilinear` gains a `demosaic_bilinear_dispatch.cpp` that self-registers. Removes the `compute.register_halide_filter(...)` / `compute.register_halide_param_filter(...)` calls from `src/cpipe/runtime/Pipeline.cpp` (both `run_to_file` and `run_file` branches) and from every test (`test_passthrough_node`, `test_node_demosaic_bilinear`, `test_node_demosaic_bilinear_vulkan`, `test_node_linearize`, `test_node_blacklevel`, `test_node_wb`, `test_node_colormatrix`, `test_golden_isp_nodes`, `color_node_fixture` if applicable). The imperative `register_halide_filter` / `register_halide_param_filter` methods remain on `ComputeContext` strictly as **test-injection seams** (used only by the in-test dispatcher in `test_compute_suite_v1_ext`); production nodes never call them.

**Acceptance criteria:**

- [x] `cpipe::runtime::HalideFilterRegistry::instance()` enumerates `passthrough_copy`, `demosaic_bilinear` after `cpipe_link_all_builtin_nodes()` is called.
- [x] `grep -rn 'register_halide_filter\|register_halide_param_filter' src/ tests/` returns hits only inside `ComputeContext.{hpp,cpp}`, the new `HalideFilterRegistry.{hpp,cpp}`, the two `*_dispatch.cpp` for the legacy AOTs, the four T1 `*_dispatch.cpp` files, and `tests/unit/test_compute_suite_v1_ext.cpp` (test-injection seam).
- [x] `cpipe-runtime` no longer needs `extern "C"` declarations of `passthrough_copy` / `demosaic_bilinear` symbols in `Pipeline.cpp`.
- [x] All P0/P1 tests + the new `test_compute_suite_v1_ext` stay green under `ctest --preset linux-debug` (no regressions).
- [x] `test_min_pipeline_dng_to_heif` integration smoke green (validates that Pipeline gets its filters from the registry, not from explicit registration).

**Verification:**

- [x] `ctest --preset linux-debug --output-on-failure` green.
- [x] `ctest --preset linux-release-clang --output-on-failure` green.

**Dependencies:** None (lands before T1). When T1's four migrated nodes ship their `*_dispatch.cpp`, they self-register via the macro introduced here.

**Files likely touched:** new `include/cpipe/runtime/HalideFilterRegistry.hpp` + `src/cpipe/runtime/HalideFilterRegistry.cpp`; new macros in `include/cpipe/sdk/registry.hpp` (or a sibling header); new `src/cpipe/nodes/passthrough_copy_dispatch.cpp`; new `src/cpipe/nodes/demosaic/demosaic_bilinear_dispatch.cpp`; modifications to `src/cpipe/runtime/Pipeline.cpp` (removal of explicit registers), `src/cpipe/runtime/ComputeContext.{hpp,cpp}` (auto-seed from registry), `src/cpipe/runtime/CMakeLists.txt` + `src/cpipe/nodes/CMakeLists.txt` (new sources), and the test files listed above (removal of explicit registers).

**Estimated scope:** M (1 new registry header/source + 2 macros + 2 new dispatch.cpp files + cleanup across ~6 host / test files).

---

#### T1 — Compute suite v1 extensions + OCIO host accessor + ABI bump

**Description.** Tail-append `submit_halide_with_params` and `submit_ocio_processor` to `cpipe_compute_suite_v1` (P2-PD-5 / P2-PD-6); tail-append `get_ocio_processor(config_path, src_cs, dst_cs)` to `cpipe_host_v1`. Bump `CPIPE_ABI_MINOR` from 2 to 3. Update `cpipe::sdk::ComputeSuite` C++ facade to expose the new entries with a one-line probe of the suite size. Add unit-test plugins that exercise both paths. Migrate the P1 four CPU-host nodes (`linearize`, `blacklevel`, `wb.dual_illuminant`, `colormatrix.dng_to_working`) to use `submit_halide_with_params` and switch their manifests from `engine: Host` to `engine: Halide` (P2-PD-47 + P2-PD-49). Note: `wb.dual_illuminant` math change (P2-PD-19) is in T11, not here — T1 lands the param-buffer plumbing and leaves the math identical to P1. Each new node's `*_dispatch.cpp` self-registers via the T0 macro (P2-PD-56).


**Acceptance criteria:**
- [x] `host->get_suite("compute", 1)` returns a suite vtable whose declared size matches the new struct; `submit_halide_with_params` and `submit_ocio_processor` are non-null on supported hosts.
- [x] P0 passthrough plugin still loads (probes only the original suite entries).
- [x] `linearize.dng_lut`, `blacklevel.dng_levels`, `wb.dual_illuminant` (P1 math), `colormatrix.dng_to_working` (P1 math) all run via `submit_halide_with_params`; manifests say `engine: Halide`.
- [x] `host->get_ocio_processor("share/cpipe/ocio/v0.1/config.ocio", "scene_linear_rec2020", "output_srgb")` returns a non-null opaque handle on the development machine.
- [x] `ctest -R "test_(compute_suite_v1_ext|metadata_suite|registry|passthrough_node)"` green.

**Verification:**
- [x] `ctest --preset linux-debug -L unit` green.
- [x] All P1 integration smokes (`test_min_pipeline_dng_to_heif`) still green.

**Dependencies:** None (after P1 closes).

**Files likely touched:** `include/cpipe/sdk/cpipe_node.h`, `include/cpipe/sdk/sdk.hpp`, `src/cpipe/runtime/ComputeContext.cpp`, `src/cpipe/runtime/HostV1.cpp`, the four migrated P1 node `.cpp` + `.json`, new `tests/unit/test_compute_suite_v1_ext.cpp`.

**Estimated scope:** L (ABI surface + ~6 nodes updated + 1 new test + facade).

---

#### T2 — cpipe-owned Vulkan dispatch + Tracy GPU

**Description.** Implement `cpipe::runtime::VulkanCommandBuffer` wrapper. Refactor `ComputeContext::submit_halide` (Vulkan path) so cpipe binds the SPIR-V module + creates `VkComputePipeline` + records `vkCmdDispatch` + signals/awaits timeline semaphores. Set `HL_VULKAN_NO_HOST_RUNTIME` for Halide AOT codegen so Halide does not embed its Vulkan runtime. Plumb Tracy GPU context (`tracy::VkCtx`) and add the five P2 spans (P2-PD-8). Retires [P1-PD-71](phase-01-walking-skeleton.md#4-phase-decisions-pd-n). **T2 implementation note:** P2-PD-59 supersedes the Halide-specific handoff wording: this task lands the cpipe-owned Vulkan dispatch primitive plus Tracy span points; Halide demosaic's direct SPIR-V handoff is not claimed by T2.

**Acceptance criteria:**
- [x] `VulkanCommandBuffer` executes a cpipe-owned Vulkan compute dispatch, creates/destroys the `VkShaderModule` + `VkPipeline`, signals a timeline semaphore, and CPU↔GPU result matches.
- [x] `VulkanCommandBuffer::submit` and `VulkanDevicePlane::wait_timeline` Tracy span points are present; local capture status is recorded under `docs/evidence/p2-t2-tracy.tracy.md`.
- [x] On a no-Vulkan machine, Vulkan tests skip and the CPU AOT path runs unchanged.
- [x] `demosaic.bilinear` Vulkan regression remains green; per P2-PD-59, Halide still owns that generated kernel's Vulkan command-buffer path.

**Verification:**
- [x] `ctest -R test_vulkan_command_buffer` green.
- [x] `ctest -R test_node_demosaic_bilinear_vulkan` green; P2-PD-59 documents why this remains a Halide-owned Vulkan dispatch regression rather than the cpipe-owned demosaic handoff.

**Dependencies:** T1.

**Files likely touched:** `include/cpipe/runtime/VulkanCommandBuffer.hpp`, `src/cpipe/runtime/VulkanCommandBuffer.cpp`, `src/cpipe/runtime/ComputeContext.cpp`, `cmake/HalideHelpers.cmake` (planned Halide no-host-runtime handoff; not touched in T2 per P2-PD-59).

**Estimated scope:** L (1 new wrapper + ComputeContext refactor + Tracy GPU + 1 new test).

---

#### T3 — Memory planner: interference-graph coloring

**Description.** Replace the linear-lifetime planner with an interference-graph coloring planner per P2-PD-9. Topo-order liveness analysis → build interference graph → greedy coloring → emit `buffer_id → physical_slot` mapping. Preserve the peak-vs-cap pre-check. Add three new fan-out / fan-in tests that exercise overlapping lifetimes.

**Acceptance criteria:**
- [x] On the P1 linear pipeline, the new planner produces an identical allocation count (regression).
- [x] On a synthetic fan-out DAG (source → {A, B} → sink), the planner allocates exactly 2 physical slots (one each for branches A and B).
- [x] On a synthetic diamond DAG (source → {A, B} → fuse → sink), the planner reuses the source slot for the sink output.
- [x] `Pipeline::set_device_memory_cap(small)` aborts with `CPIPE_OOM` when peak exceeds.

**Verification:**
- [x] `ctest -R test_memory_planner` green (extended).
- [x] `ctest -R test_diamond_dag_parallel` still green.

**Dependencies:** T1 (compute suite changes do not block, but tests do).

**Files likely touched:** `include/cpipe/runtime/MemoryPlanner.hpp`, `src/cpipe/runtime/MemoryPlanner.cpp`, `tests/unit/test_memory_planner.cpp`.

**Estimated scope:** M (1 file rewritten + 3 unit-test cases added).

---

#### T4 — Precision planner: auto-insert + `precision_convert` built-in

**Description.** Implement `com.cpipe.precision_convert` built-in node (P2-PD-32) with Halide AOT generators for the four supported conversions. Implement `PrecisionPlanner::auto_insert(graph)` (P2-PD-10) that splices `precision_convert` at producer/consumer port mismatches. Scheduler diagnostics log the inserted nodes via spdlog. `CPIPE_BAD_PRECISION` fires only when no conversion path exists.

**Acceptance criteria:**
- [x] Pipeline with `node_A.output = R16G16B16A16_SFLOAT → node_B.input = R8G8B8A8_UNORM` loads, scheduler trace shows the implicit `precision_convert`, and round-trip is within 1 LSB.
- [x] Pipeline with `R16_UINT → R8G8B8A8_UNORM` aborts with `CPIPE_BAD_PRECISION` (demosaic required).
- [x] `precision_convert` is not addressable from `pipeline-v0.3.json` (parser rejects).

**Verification:**
- [x] `ctest -R test_precision_planner` green.
- [x] `ctest -R test_node_precision_convert` green.

**Dependencies:** T1.

**Files likely touched:** `include/cpipe/runtime/PrecisionPlanner.hpp`, `src/cpipe/runtime/PrecisionPlanner.cpp`, `src/cpipe/nodes/precision/precision_convert.{cpp,json,_generator.cpp}`, `tests/unit/test_precision_planner.cpp`, `tests/unit/test_node_precision_convert.cpp`.

**Estimated scope:** L (1 planner rewrite + 1 built-in node + 2 tests).

---

### Checkpoint A — after T1–T4

- [x] All four tasks merged; `main` is green.
- [x] Compute suite carries `submit_halide_with_params` + `submit_ocio_processor`; `CPIPE_ABI_MINOR == 3`.
- [x] cpipe-owned Vulkan dispatch runs `demosaic.bilinear`; Tracy GPU evidence filed.
- [x] Memory planner is interference-graph coloring; three fan-out / fan-in tests pass.
- [x] Precision planner auto-inserts conversions; `precision_convert` built-in registered.
- [x] P1 min-pipeline integration smoke still green; no regression on P1 unit tests.
- [x] Review: ABI grew at the tail only; P0 passthrough still loads.

---

### Phase 2.B — Demosaic + lens correction

#### T5 — pipeline-v0.3 + node-v0.2 schemas

**Description.** Author `schemas/pipeline-v0.3.json` (param blocks live; reject v0.2). Author `schemas/node-v0.2.json` (param-schema entries: `name`, `type`, `enum_values?`, `range?`, `default?`). Implement `Pipeline::load` schema-bump logic and the v0.2 → v0.3 migration of the P1 fixtures (`min-pipeline.cpipe.json`). Reject v0.2 with the existing "schema version mismatch" message; v0.1 also rejected. Update one P1 node manifest (e.g. `linearize.dng_lut`) to declare an empty `params` array per node-v0.2 to prove the new schema works.

**Acceptance criteria:**
- [x] `pipeline-v0.3.json` parses; v0.2 fixture loads after migration; v0.2 / v0.1 unmigrated fixtures fail with a clear message.
- [x] `node-v0.2.json` parses; node-v0.1 manifests forward-compat load.
- [x] `Pipeline::load` rejects a v0.3 fixture whose `params.target` is outside the enum.

**Verification:**
- [x] `ctest -R test_pipeline_load` green (extended).
- [x] `ctest -R test_node_manifest_v0_2` green.

**Dependencies:** Checkpoint A.

**Files likely touched:** `schemas/pipeline-v0.3.json`, `schemas/node-v0.2.json`, `src/cpipe/runtime/Pipeline.cpp`, `examples/pipelines/min-pipeline.cpipe.json` (migrated), `tests/unit/test_pipeline_load.cpp`, `tests/unit/test_node_manifest_v0_2.cpp`.

**Estimated scope:** M (2 schemas + parser changes + 2 tests).

---

#### T6 — demosaic.rcd (CPU + Vulkan AOT) + golden

**Description.** Implement `com.cpipe.demosaic.rcd` from Sanz Rodríguez & Bayón 2014 — algorithm only, no GPLv3 source consulted. Halide generator targets both `${Halide_HOST_TARGET}` (CPU) and `${Halide_HOST_TARGET}-vulkan-vk_float16` (P2-PD-51). Four-CFA support via uniform pattern (P2-PD-48 carries the wider CFA gate). Author the checked-in golden EXR pair; P2-PD-61 records why this T6 fixture is deterministic cpipe self-reference instead of the planned RT 5.10 reference. Update `demosaic.bilinear` to use the CFA uniform path too (so all three demosaic nodes share the four-CFA semantics).

**Acceptance criteria:**
- [x] `process()` produces FP16 RGBA on a 16×16 synthetic Bayer; PSNR ≥ 40 dB against the checked-in deterministic cpipe self-reference (P2-PD-61 carries the RT 5.10 refresh).
- [x] CPU and Vulkan AOT objects build for `demosaic.rcd`; the conditional Vulkan smoke runs when `CPIPE_VULKAN_AVAILABLE=ON`, while direct cpipe-owned Halide command-buffer dispatch remains carried by P2-PD-59.
- [x] All four 2×2 Bayer patterns (RGGB/BGGR/GRBG/GBRG) produce correct demosaic.
- [x] `out_metadata.clear_cfa()` and `add_applied_step("demosaic")` validated by manifest at freeze.

**Verification:**
- [x] `ctest -R test_node_demosaic_rcd` green.
- [x] `ctest -L golden` PSNR ≥ 40 dB for `demosaic.rcd`.

**Dependencies:** T5.

**Files touched:** `src/cpipe/nodes/demosaic/demosaic_rcd.{cpp,json,_dispatch.cpp,_generator.cpp}`, `src/cpipe/nodes/demosaic/demosaic_bilinear*.cpp` (CFA-pattern uniform), `tests/unit/test_node_demosaic_rcd*.cpp`, `tests/golden/demosaic.rcd/{in,out}.exr`, `tests/golden/demosaic.rcd/reference.md`.

**Estimated scope:** L (RCD source + bilinear uniform update + tests + 2 LFS goldens).

---

#### T7 — demosaic.amaze + golden

**Description.** Implement `com.cpipe.demosaic.amaze` from Emil Martinec's public algorithm description — algorithm only, no GPLv3 source consulted. Halide CPU AOT (no Vulkan AOT in P2). Author RT 5.10 reference golden EXR pair.

**Acceptance criteria:**
- [ ] PSNR ≥ 40 dB against RT 5.10 AMaZE reference on the 16×16 synthetic Bayer fixture.
- [ ] All four CFA patterns supported.
- [ ] `out_metadata.add_applied_step("demosaic")` and `clear_cfa()`.

**Verification:**
- [ ] `ctest -R test_node_demosaic_amaze` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB for `demosaic.amaze`.

**Dependencies:** T6 (T6 unlocks the CFA pattern uniform infrastructure).

**Files likely touched:** `src/cpipe/nodes/demosaic/demosaic_amaze.{cpp,json,_generator.cpp}`, `tests/unit/test_node_demosaic_amaze.cpp`, `tests/golden/demosaic.amaze/{in,out}.exr`.

**Estimated scope:** M (3 source + 1 test + 2 LFS goldens).

---

#### T8 — demosaic.quad_bayer_remosaic + Pixel 8 Pro QBC fixture + golden

**Description.** Implement `com.cpipe.demosaic.quad_bayer_remosaic` per P2-PD-15: 4×4 → 2×2 edge-aware gradient blend (Sony QBC whitepaper + MathWorks BSD reference). Halide CPU AOT. Widen the `DngReader` CFA gate to accept 4×4 when a remosaic node is downstream (P2-PD-15). Procure `tests/corpus/pixel8pro-qbc.dng` (P2-PD-37) under CC0 / CC-BY (raw.pixls.us preferred) and commit the cropped 3840×2880 fixture + provenance to `tests/corpus/pixel8pro-qbc.source.md`. Author cpipe self-referenced golden (P2-PD-36 — no public reference).

**Acceptance criteria:**
- [ ] Cropped Pixel 8 Pro QBC DNG loads; `DngReader` reports 4×4 CFA; `Pipeline::load` accepts when remosaic is upstream of every demosaic.
- [ ] `demosaic.quad_bayer_remosaic` produces 2×2 Bayer at same dims; RGGB pattern checked.
- [ ] Golden PSNR ≥ 40 dB against cpipe self-reference.
- [ ] `out_metadata.set_cfa(RGGB-2x2)` and `add_applied_step("quad_bayer_remosaic")`.

**Verification:**
- [ ] `ctest -R test_node_demosaic_quad_bayer_remosaic` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB.
- [ ] `cpipe run tests/corpus/pixel8pro-qbc.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o /tmp/out.heif` exits 0 (T20 wires the full pipeline; verified there).

**Dependencies:** T6.

**Files likely touched:** `src/cpipe/ingest/dng/DngReader.cpp` (widen CFA gate), `src/cpipe/nodes/demosaic/demosaic_quad_bayer_remosaic.{cpp,json,_generator.cpp}`, `tests/corpus/pixel8pro-qbc.dng` (LFS), `tests/corpus/pixel8pro-qbc.source.md`, `tests/golden/demosaic.quad_bayer_remosaic/{in,out}.exr` (LFS), `tests/unit/test_node_demosaic_quad_bayer_remosaic.cpp`.

**Estimated scope:** L (3 source + DngReader change + 1 LFS fixture + 2 LFS goldens + 1 test + 1 provenance doc).

---

#### T9 — lens.shading_gainmap + golden (resolves Q6)

**Description.** Implement `com.cpipe.lens.shading_gainmap` (P2-PD-16). Parser unpacks the GainMap opcode from `BufferMetadata.ext_blobs["com.cpipe.dng.opcode_list_2_bytes"]`; Halide generator applies per-Bayer-channel gain. Multi-plane support (4 planes for QBC) per P2-PD-18 — resolves [Architecture §17 Q6](architecture.md#17-open-questions). Position in DAG: between `linearize` and `blacklevel` (P2-PD-33). Author cpipe self-referenced golden.

**Acceptance criteria:**
- [ ] OpcodeList2 GainMap parsing handles 1-plane (regular Bayer) and 4-plane (QBC) variants.
- [ ] Generator applies per-channel gain bilinearly resampled to image dims.
- [ ] On the Pixel 8 Pro Bayer DNG, output retains within-1-LSB of a hand-computed reference; on the Pixel 8 Pro QBC DNG, 4-plane behaves correctly.
- [ ] Architecture §17 Q6 updated to "Resolved in P2-PD-18".

**Verification:**
- [ ] `ctest -R test_node_lens_shading_gainmap` green.
- [ ] `ctest -R test_opcode_list_2_parser` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB for `lens.shading_gainmap`.

**Dependencies:** T8 (needs the QBC fixture).

**Files likely touched:** `include/cpipe/ingest/dng_opcode/OpcodeList2.hpp`, `src/cpipe/ingest/dng_opcode/OpcodeList2.cpp`, `src/cpipe/nodes/lens/shading_gainmap.{cpp,json,_generator.cpp}`, `tests/unit/test_node_lens_shading_gainmap.cpp`, `tests/unit/test_opcode_list_2_parser.cpp`, `tests/golden/lens.shading_gainmap/{in,out}.exr`, `docs/architecture.md` (§17 Q6).

**Estimated scope:** L (1 parser + 3 node files + 2 tests + 2 LFS goldens + architecture doc edit).

---

#### T10 — lens.dng_opcode_list_3 dispatcher + 5 opcodes + golden

**Description.** Implement `com.cpipe.lens.dng_opcode_list_3` (P2-PD-17). The node reads `ext_blobs["com.cpipe.dng.opcode_list_3_bytes"]` and dispatches to in-node sub-implementations for `WarpRectilinear` (bicubic resample), `FixVignetteRadial` (4th-order radial), `FixBadPixelsConstant`, `FixBadPixelsList`, and `TrimBounds`. GainMap is **not** in this node (it lives in T9). Halide CPU AOT. Position in DAG: after `demosaic` (P2-PD-33). Author colour-hdri Python reference golden (P2-PD-35).

**Acceptance criteria:**
- [ ] All five opcodes execute correctly on synthetic patterns.
- [ ] Pixel 8 Pro Bayer DNG's OpcodeList3 bytes parse without error; resulting image has corrected lens distortion (subjective check + PSNR ≥ 40 dB vs colour-hdri reference on the cropped fixture).
- [ ] OpcodeList3 with an unknown opcode logs and skips (graceful degrade).

**Verification:**
- [ ] `ctest -R test_node_lens_dng_opcode_list_3` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** T6 (demosaic must be available so opcode_list_3 has RGB input).

**Files likely touched:** `include/cpipe/ingest/dng_opcode/OpcodeList3.hpp`, `src/cpipe/ingest/dng_opcode/OpcodeList3.cpp`, `src/cpipe/nodes/lens/dng_opcode_list_3.{cpp,json,_generator.cpp}`, `tests/unit/test_node_lens_dng_opcode_list_3.cpp`, `tests/golden/lens.dng_opcode_list_3/{in,out}.exr`, `tools/golden/colour_hdri_opcode3_render.py`.

**Estimated scope:** L (1 dispatcher + 3 node files + 1 test + 2 LFS goldens + 1 reference script).

---

### Checkpoint B — after T5–T10

- [ ] Schema-bump live; all P1 fixtures migrated to v0.3.
- [ ] Three demosaic nodes (RCD / AMaZE / Quad Bayer remosaic) shipped with goldens green; bilinear runs on the four-CFA uniform path.
- [ ] OpcodeList2 GainMap (incl. multi-plane Q6) + OpcodeList3 (5 opcodes) shipped; goldens green.
- [ ] Pixel 8 Pro QBC DNG fixture committed.
- [ ] `cpipe run tests/corpus/pixel8pro-qbc.dng -p (partial QBC pipeline) -o /tmp/out.exr` runs through demosaic + lens correction without crash (manual smoke).
- [ ] Architecture §17 Q6 marked Resolved.

---

### Phase 2.C — WB / matrix / denoise / tone / color / display / output / glue

#### T11 — wb.dual_illuminant full upgrade + golden

**Description.** Replace the P1 minimal `invert(AsShotNeutral)` with the full CCT-interpolated dual-illuminant transform (P2-PD-19). Halide AOT via `submit_halide_with_params` (the interpolated 3×3 matrix is passed in the param buffer; CPU side performs the Brent CCT search and matrix lerp before submission). Reference: Adobe DNG 1.7 Spec §6.4.3 + Research 13 §3.6. Regenerate the P1 wb golden against the new math.

**Acceptance criteria:**
- [ ] CCT search converges on the Pixel 8 Pro DNG to within 0.5 K of a hand-computed reference.
- [ ] Reciprocal-CCT weight matches Adobe DNG spec; ColorMatrix interpolation linear; ForwardMatrix interpolation linear.
- [ ] Camera-WB diag emitted via metadata for downstream `colormatrix.dng_to_working`.
- [ ] PSNR ≥ 40 dB against RT 5.10 same-CCT reference on the Pixel 8 Pro fixture.

**Verification:**
- [ ] `ctest -R test_node_wb` green (regenerated).
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** Checkpoint B (the wb upgrade can land anytime after the planner is in place, but slots here so it runs against the full pipeline).

**Files likely touched:** `src/cpipe/nodes/wb/dual_illuminant.{cpp,json,_generator.cpp}` (rewritten), `tests/unit/test_node_wb.cpp` (extended), `tests/golden/wb.dual_illuminant/{in,out}.exr` (regenerated).

**Estimated scope:** M (1 rewrite + 1 test extend + 2 LFS goldens regenerated).

---

#### T12 — wb.greyworld_auto + colormatrix.dng_to_working upgrade + golden

**Description.** Implement `com.cpipe.wb.greyworld_auto` (P2-PD-20) — host engine, CPU plugin loop, emits the same metadata surface as `wb.dual_illuminant`. Upgrade `com.cpipe.colormatrix.dng_to_working` (P2-PD-21) to consume the interpolated matrix from metadata; Bradford + Rec.2020 stay baked constants. Regenerate the colormatrix golden.

**Acceptance criteria:**
- [ ] `wb.greyworld_auto` produces a neutral estimate within 5 % on a synthetic gray-mean fixture.
- [ ] `colormatrix.dng_to_working` with the upgraded `wb.dual_illuminant` upstream produces Rec.2020 D65 to within 1 LSB of an RT 5.10 same-CCT reference.
- [ ] PSNR ≥ 40 dB on both nodes' goldens.

**Verification:**
- [ ] `ctest -R test_node_wb_greyworld_auto` green.
- [ ] `ctest -R test_node_colormatrix` green (extended).
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** T11.

**Files likely touched:** `src/cpipe/nodes/wb/greyworld_auto.{cpp,json}`, `src/cpipe/nodes/colormatrix/dng_to_working.{cpp,json,_generator.cpp}` (rewritten), `tests/unit/test_node_wb_greyworld_auto.cpp`, `tests/unit/test_node_colormatrix.cpp` (extended), `tests/golden/wb.greyworld_auto/{in,out}.exr`, `tests/golden/colormatrix.dng_to_working/{in,out}.exr` (regenerated).

**Estimated scope:** M (2 new node files + 1 rewrite + 2 tests + 4 LFS goldens).

---

#### T13 — denoise.guided_filter + denoise.wavelet_bayes_shrink + goldens

**Description.** Implement `com.cpipe.denoise.guided_filter` (He 2010) — fast preview / chroma path. Implement `com.cpipe.denoise.wavelet_bayes_shrink` (Chang 2000 + scikit-image BSD3 reference) — chroma denoise. Both Halide CPU AOT. Author goldens (scikit-image reference for wavelet; cpipe self-reference for guided filter).

**Acceptance criteria:**
- [ ] `denoise.guided_filter` PSNR ≥ 40 dB on cpipe self-reference.
- [ ] `denoise.wavelet_bayes_shrink` PSNR ≥ 40 dB on scikit-image reference for `R16G16B16A16_SFLOAT` chroma.
- [ ] Both nodes set `applied_steps += <id>` and propagate metadata unchanged otherwise.

**Verification:**
- [ ] `ctest -R "test_node_denoise_(guided_filter|wavelet_bayes_shrink)"` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB for both.

**Dependencies:** Checkpoint B (the planner / suite changes are in).

**Files likely touched:** `src/cpipe/nodes/denoise/guided_filter.{cpp,json,_generator.cpp}`, `src/cpipe/nodes/denoise/wavelet_bayes_shrink.{cpp,json,_generator.cpp}`, 2 unit tests, 4 LFS goldens, `tools/golden/skimage_wavelet_render.py`.

**Estimated scope:** M (6 source + 2 tests + 4 LFS goldens + 1 reference script).

---

#### T14 — denoise.bm3d + golden + sharpen.edge_aware_usm

**Description.** Implement `com.cpipe.denoise.bm3d` (P2-PD-23) — two-stage Halide schedule (hard-threshold step1 + Wiener step2). NoiseProfile β/λ drives per-channel sigma; `sigma_override` param overrides. Reference: Dabov 2007 + Mäkinen 2020 BSD; IPOL Mäkinen output as golden reference (P2-PD-35). Implement `com.cpipe.sharpen.edge_aware_usm` (He 2010 / 2013 fast guided filter) — Halide CPU AOT. RT 5.10 USM reference golden.

**Acceptance criteria:**
- [ ] BM3D on a 16×16 noisy synthetic patch reduces RMSE by ≥ 80 % vs ground truth; PSNR ≥ 40 dB against IPOL Mäkinen reference.
- [ ] `sigma_override` param accepts a single float and overrides the metadata-derived sigma.
- [ ] Sharpen produces visibly enhanced edges; PSNR ≥ 40 dB against RT 5.10 USM reference.

**Verification:**
- [ ] `ctest -R test_node_denoise_bm3d` green.
- [ ] `ctest -R test_node_sharpen_edge_aware_usm` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB for both.

**Dependencies:** T13.

**Files likely touched:** `src/cpipe/nodes/denoise/bm3d.{cpp,json,_generator.cpp}` (split into `bm3d_step1_generator.cpp` and `bm3d_step2_generator.cpp` if Halide pipeline is too big for one file), `src/cpipe/nodes/sharpen/edge_aware_usm.{cpp,json,_generator.cpp}`, 2 unit tests, 4 LFS goldens, `tools/golden/ipol_bm3d_render.py`.

**Estimated scope:** L (BM3D is the heavy item — Research 07 OQ 2 budgets 3 weeks).

---

#### T15 — tone.filmic_rgb + tone.aces_filmic + tone.reinhard + goldens

**Description.** Implement the three global tone mappers per P2-PD-22. `filmic_rgb` from Aurelien Pierre 2018 + darktable filmic-RGB module manual — re-implementation, no GPLv3 source consulted; `aces_filmic` from the Narkowicz 2016 fit (Apache-2 compat); `reinhard` from the closed-form Reinhard 2002 paper. Halide CPU AOT. Goldens: cpipe self-reference for `filmic_rgb` (P2-PD-36); NumPy Narkowicz reference for `aces_filmic`; closed-form reference for `reinhard`.

**Acceptance criteria:**
- [ ] All three nodes produce displayable output on a synthetic gradient (0..16 stops).
- [ ] Goldens PSNR ≥ 40 dB against their respective references.
- [ ] `applied_steps += <node_id>`.

**Verification:**
- [ ] `ctest -R "test_node_tone_(filmic_rgb|aces_filmic|reinhard)"` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB on all three.

**Dependencies:** Checkpoint B.

**Files likely touched:** `src/cpipe/nodes/tone/{filmic_rgb,aces_filmic,reinhard}.{cpp,json,_generator.cpp}`, 3 unit tests, 6 LFS goldens, `tools/golden/narkowicz_aces_render.py`.

**Estimated scope:** M (9 source + 3 tests + 6 LFS goldens + 1 reference script).

---

#### T16 — tone.mertens_local + golden

**Description.** Implement `com.cpipe.tone.mertens_local` from IPOL 2018 BSD ("An Implementation of the Exposure Fusion Algorithm") — re-implementation. SDR-only in P2; HDR-pyramid variant deferred to v1.1 per Research 07 OQ 6. Halide CPU AOT. Golden against IPOL reference (P2-PD-35).

**Acceptance criteria:**
- [ ] Mertens fusion on a synthetic exposure stack produces a single SDR image.
- [ ] PSNR ≥ 40 dB against IPOL Mertens reference.
- [ ] `applied_steps += "tone_mertens_local"`.

**Verification:**
- [ ] `ctest -R test_node_tone_mertens_local` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** T15.

**Files likely touched:** `src/cpipe/nodes/tone/mertens_local.{cpp,json,_generator.cpp}`, `tests/unit/test_node_tone_mertens_local.cpp`, `tests/golden/tone.mertens_local/{in,out}.exr`, `tools/golden/ipol_mertens_render.py`.

**Estimated scope:** M (3 source + 1 test + 2 LFS goldens + 1 reference script).

---

#### T17 — color.3d_lut + .cube/.spi3d loaders + golden

**Description.** Implement `com.cpipe.color.3d_lut` (P2-PD-25) — Halide AOT tetrahedral apply. CPU-side `.cube` and `.spi3d` loader in `cpipe::color::Cube3dLutLoader`. `lut_path` param mandatory. Synthetic LUT golden (cpipe self-reference, P2-PD-36).

**Acceptance criteria:**
- [ ] `.cube` and `.spi3d` loaders parse standard reference files; identity LUT round-trips within 1 LSB.
- [ ] Tetrahedral interpolation produces correct output on a 33³ LUT (verified by direct math comparison on a few colors).
- [ ] PSNR ≥ 40 dB on golden.
- [ ] Schema validates `lut_path` as required string.

**Verification:**
- [ ] `ctest -R test_node_color_3d_lut` green.
- [ ] `ctest -R test_cube_3d_lut_loader` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** Checkpoint B.

**Files likely touched:** `include/cpipe/color/Cube3dLutLoader.hpp`, `src/cpipe/color/Cube3dLutLoader.cpp`, `src/cpipe/nodes/color/lut3d.{cpp,json,_generator.cpp}`, 2 unit tests, 2 LFS goldens + 1 sample `.cube` (LFS).

**Estimated scope:** M (2 loaders + 3 node files + 2 tests + 3 LFS).

---

#### T18 — fusion.hdr_plus_derivative placeholder

**Description.** Register `com.cpipe.fusion.hdr_plus_derivative` per P2-PD-31. `process()` returns `outputs[0] = inputs[0]` (ref frame); `applied_steps += "burst_fusion_stub"`. Manifest carries two inputs and one output. Smoke test confirms registration + passthrough behavior.

**Acceptance criteria:**
- [ ] Node appears in `host->iterate_registry()`.
- [ ] `Pipeline::load` accepts a pipeline binding both input ports to the same buffer; `run` produces ref-frame output.
- [ ] `applied_steps` reflects the stub.

**Verification:**
- [ ] `ctest -R test_node_fusion_hdr_plus_derivative` green.

**Dependencies:** Checkpoint B.

**Files likely touched:** `src/cpipe/nodes/fusion/hdr_plus_derivative.{cpp,json}`, `tests/unit/test_node_fusion_hdr_plus_derivative.cpp`.

**Estimated scope:** S (placeholder).

---

#### T19 — color.scene_linear_to_display + OCIO config v0.2 + Looks

**Description.** Implement `com.cpipe.color.scene_linear_to_display` (P2-PD-26) via `submit_ocio_processor` (P2-PD-6). `target` param enum: `sRGB` and `BT2020-PQ` supported in P2; `DisplayP3` and `BT2020-HLG` declared in schema but return `CPIPE_UNSUPPORTED`. Author `share/cpipe/ocio/v0.2/config.ocio` (P2-PD-30) — 4 colorspaces / 4 displays / 2 looks. Bundle FP16 1D-LUTs for the looks under `share/cpipe/ocio/v0.2/luts/`. Author cpipe self-referenced golden.

**Acceptance criteria:**
- [ ] OCIO config v0.2 loads via `host->get_ocio_processor`.
- [ ] `target: sRGB` produces `R8G8B8A8_UNORM` sRGB; `target: BT2020-PQ` produces `R16G16B16A16_UNORM` (10-bit data top-aligned).
- [ ] `target: DisplayP3` / `BT2020-HLG` return `CPIPE_UNSUPPORTED`.
- [ ] Standard SDR + Standard HDR looks load.
- [ ] Golden PSNR ≥ 40 dB.

**Verification:**
- [ ] `ctest -R test_node_color_scene_linear_to_display` green.
- [ ] `ctest -R test_ocio_config_v0_2` green.
- [ ] `ctest -L golden` PSNR ≥ 40 dB.

**Dependencies:** Checkpoint B + T1 (`submit_ocio_processor`) + T2 (Vulkan dispatch).

**Files likely touched:** `include/cpipe/color/OcioVulkanProcessor.hpp`, `src/cpipe/color/OcioVulkanProcessor.cpp`, `src/cpipe/nodes/color/scene_linear_to_display.{cpp,json}`, `share/cpipe/ocio/v0.2/config.ocio` + LUTs, 2 unit tests, 2 LFS goldens.

**Estimated scope:** L (1 OCIO wrapper + 2 node files + OCIO config + 2 tests + 2 LFS).

---

#### T20 — output.heif_sdr refactor + output.heif_hdr_pq + ICC v4.4 writer

**Description.** Refactor `com.cpipe.output.heif_sdr` to be a pure encoder (P2-PD-27): input precision changes to `R8G8B8A8_UNORM`; OCIO logic removed; ICC + CICP unchanged. Implement `com.cpipe.output.heif_hdr_pq` (P2-PD-28): kvazaar Main10, CICP `(9, 16, 9)`, mdcv static + clli encode-time computed. Implement `cpipe::color::IccV4_4Writer` (P2-PD-29) for the HDR ICC profile with `lutAtoBfloatType`. Migrate `examples/pipelines/min-pipeline.cpipe.json` to use the new sink contract (P1 fixture path).

**Acceptance criteria:**
- [ ] SDR pipeline (full classic with `target: sRGB`) on `pixel8pro.dng` produces `heif-info`-valid SDR HEIF; CICP `(1, 13, 1)`; sRGB ICC v4 present; no libx265 in linkage.
- [ ] HDR pipeline (full classic with `target: BT2020-PQ`) on `pixel8pro.dng` produces `heif-info`-valid HDR HEIF; CICP `(9, 16, 9)`; ICC v4.4 with `cicp` tag present; mdcv/clli present; clli MaxCLL/MaxFALL non-zero.
- [ ] `cpipe::color::IccV4_4Writer` round-trips a sanity ICC through `lcms2` (basic profile load).
- [ ] Quad Bayer DNG runs through the SDR pipeline without crash (the visual outcome is recorded; per DoD #3 "subjectively acceptable").

**Verification:**
- [ ] `ctest -R "test_(output_heif_sdr|output_heif_hdr_pq|icc_v4_4_writer)"` green.
- [ ] `heif-info /tmp/out-hdr.heif` confirms `Main10`, CICP `(9, 16, 9)`, mdcv, clli.

**Dependencies:** T19.

**Files likely touched:** `include/cpipe/color/IccV4_4Writer.hpp`, `src/cpipe/color/IccV4_4Writer.cpp`, `src/cpipe/color/HeifWriter.cpp` (extended), `src/cpipe/nodes/output/heif_sdr.{cpp,json}` (refactored), `src/cpipe/nodes/output/heif_hdr_pq.{cpp,json}` (new), 3 unit tests, `examples/pipelines/min-pipeline.cpipe.json` (migrated).

**Estimated scope:** L (1 ICC writer + HeifWriter extend + 2 nodes + 3 tests + 1 fixture migration).

---

#### T21 — Full pipeline integration smokes + Tracy + tag

**Description.** Land `examples/pipelines/full-classic-pipeline.cpipe.json` (SDR) and `examples/pipelines/full-classic-pipeline-hdr.cpipe.json` (HDR PQ) per P2-PD-33. Author the two integration smokes (P2-PD-40): `test_full_classic_pipeline_dng_to_heif_sdr` and `test_full_classic_pipeline_dng_to_heif_hdr`. Capture local Tracy GPU + CPU trace for the SDR run (file under `docs/evidence/p2-t21-tracy.tracy.md`). Update [`roadmap.md` §5](roadmap.md#5-phase-2--classic-nodes--hdr-tag-v03) to "shipped"; update [`README.md`](../README.md) "Current Status" to mirror; update [`architecture.md` §17](architecture.md#17-open-questions) Q6 → Resolved (already touched in T9, this is the final confirmation). Tag `v0.3`.

**Acceptance criteria:**
- [ ] `cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o /tmp/out.heif` exits 0; `heif-info` valid SDR HEIF.
- [ ] `cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/full-classic-pipeline-hdr.cpipe.json -o /tmp/out-hdr.heif` exits 0; `heif-info` valid HDR HEIF with PQ.
- [ ] `cpipe run tests/corpus/pixel8pro-qbc.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o /tmp/out-qbc.heif` exits 0 (full QBC path: remosaic → bayer pipeline → SDR HEIF).
- [ ] `test_full_classic_pipeline_dng_to_heif_sdr` PSNR ≥ 37 dB vs RT 5.10 reference.
- [ ] `test_full_classic_pipeline_dng_to_heif_hdr` PSNR ≥ 37 dB vs cpipe pre-HEIF Rec.2020-PQ-display self-reference.
- [ ] Tracy capture (`-DCPIPE_ENABLE_TRACY=ON`) shows the eight P2 spans on the SDR smoke run.
- [ ] roadmap.md §5 P2 row updated to "shipped"; README.md "Current Status" mirrors; phase-02-classic-nodes-hdr.md §12 "What Shipped / What Slipped" filled in.
- [ ] `git tag --list 'v0.3'` returns `v0.3`; GitHub Release notes attached.

**Verification:**
- [ ] DoD §11 commands all green.

**Dependencies:** T20.

**Files likely touched:** `examples/pipelines/full-classic-pipeline.cpipe.json`, `examples/pipelines/full-classic-pipeline-hdr.cpipe.json`, `tests/integration/test_full_classic_pipeline_dng_to_heif_sdr.cpp`, `tests/integration/test_full_classic_pipeline_dng_to_heif_hdr.cpp`, `docs/roadmap.md`, `docs/phase-02-classic-nodes-hdr.md` (§12), `README.md`.

**Estimated scope:** M (2 example pipelines + 2 integration tests + doc updates).

---

### Checkpoint C — P2 DoD

- [ ] §11 verification commands all green.
- [ ] Latest `main` CI green on the release-candidate commit (P2-PD-4 waiver).
- [ ] No regressions on P1 unit / integration tests.
- [ ] `v0.3` tag pushed; GitHub Release published.

---

## 8. Architecture Notes (P2-specific)

- **Halide multi-target build.** Only `demosaic.bilinear` (P1 carry) and `demosaic.rcd` (P2-PD-51) build the `${Halide_HOST_TARGET}-vulkan-vk_float16` multi-target. All other Halide generators in P2 ship CPU AOT only; wider Vulkan AOT is P3+ work tied to the IQA harness exposing GPU paths.
- **OCIO Vulkan compute path.** `OcioVulkanProcessor` is constructed at runtime via `host->get_ocio_processor(config, src, dst)`, which calls `OCIO::Config::CreateFromFile` once and caches the resulting `ConstGPUProcessorRcPtr` keyed on (config, src, dst). The wrapper emits OCIO's Vulkan GLSL via `OCIO::GpuShaderDesc::CreateShaderDesc` + `OCIO::GPU_LANGUAGE_VK_GLSL_4_50`, compiles it through glslang to SPIR-V at runtime, and binds the SPIR-V into a cpipe `VkComputePipeline`. Inputs and outputs are VMA-backed Vulkan images; LUTs are pre-uploaded to dedicated `R32_SFLOAT 1D image` and `R16G16B16A16_SFLOAT 3D image` VMA allocations.
- **HDR HEIF round-trip for the integration smoke.** The HDR re-decode helper (`HeifReader` extended) reads the BT.2100-PQ HEIF via libde265 + libheif, applies the PQ EOTF (procedural per Research 13 §3.12), Bradford-CAT-inverts to scene-linear Rec.2020 D65, and feeds the OIIO PSNR check.
- **kvazaar Main10 invocation.** kvazaar 2.3 supports 10-bit Main10 via `-i input.yuv --input-bitdepth 10 --output-bitdepth 10 --preset medium`. libheif 1.20.1 passes the bit-depth down to the kvazaar plugin; cpipe sets the plugin params via `heif_encoder_set_parameter_integer(enc, "bit-depth", 10)` then `heif_encoder_set_parameter_string(enc, "preset", "medium")`. The vcpkg overlay port for kvazaar (P1-PD-62) is unchanged.
- **CFA-pattern uniform.** P2 widens `demosaic.bilinear` (P1) and ships `demosaic.rcd` / `demosaic.amaze` with a CFA-pattern uniform (`int4 cfa_pattern = {0,1,1,2}` for RGGB, `{2,1,1,0}` for BGGR, etc.). `submit_halide_with_params` passes the uniform via the param buffer; the Halide generator branches on the repeated pattern expression.
- **GainMap multi-plane semantics.** When 4-plane GainMap is detected on a 4×4 QBC DNG, `lens.shading_gainmap` schedules four parallel Halide passes (one per Bayer sub-CFA) and the planner allocates four scratch buffers via interference-graph coloring. The four results overlay onto the same output buffer at their respective offset positions. The 2×2 path uses the same generator with a 1-plane unwrap.
- **Halide Vulkan AOT handoff.** The planned `HL_VULKAN_NO_HOST_RUNTIME` path is not present in the local Halide v21 package. T2 therefore uses `VulkanCommandBuffer` for caller-owned SPIR-V dispatch and records the direct Halide demosaic handoff as P2-PD-59 carry.
- **Reverting P1 host engines.** P1 PD-56 / PD-60 / PD-22 are reverted in T1 / T11 / T12. P2 release notes call out the manifest engine change so anyone running P1 plugins against P2 hosts sees the Halide path (the suite is backward-compat — old plugins read v1 entries, new nodes use the v2 tail).

---

## 9. Tests in P2 (additions to P1)

| #  | Test                                              | Layer       | Asserts |
|----|---------------------------------------------------|-------------|---------|
| 30 | `test_compute_suite_v1_ext`                       | unit        | `submit_halide_with_params` + `submit_ocio_processor` round-trips |
| 31 | `test_vulkan_command_buffer`                      | unit        | cpipe-owned Vulkan compute dispatch; CPU↔GPU storage-buffer result matches |
| 32 | `test_memory_planner` (extended)                  | unit        | linear / fan-out / diamond DAG allocations |
| 33 | `test_precision_planner`                          | unit        | auto-insert path + abort path |
| 34 | `test_node_precision_convert`                     | unit        | the four supported conversions |
| 35 | `test_pipeline_load` (extended)                   | unit        | v0.3 accepts, v0.2 rejects, v0.1 rejects, param-enum violations rejected |
| 36 | `test_node_manifest_v0_2`                         | unit        | param-schema parsing + validation |
| 37 | `test_node_demosaic_rcd*`                         | unit        | RCD demosaic on 16×16 synthetic Bayer + four-CFA + metadata update + conditional Vulkan AOT smoke |
| 38 | `test_node_demosaic_amaze`                        | unit        | algorithm correctness + RT 5.10 reference + four-CFA |
| 39 | `test_node_demosaic_quad_bayer_remosaic`          | unit        | 4×4 → 2×2 edge-aware on synthetic and Pixel 8 Pro QBC fixture |
| 40 | `test_opcode_list_2_parser`                       | unit        | GainMap 1-plane and 4-plane parsing |
| 41 | `test_node_lens_shading_gainmap`                  | unit        | Gain applied; Q6 resolution check on 4-plane variant |
| 42 | `test_node_lens_dng_opcode_list_3`                | unit        | All five opcodes on synthetic patterns + Pixel 8 Pro fixture |
| 43 | `test_node_wb` (extended)                         | unit        | CCT search; reciprocal-CCT weight; matrix interpolation |
| 44 | `test_node_wb_greyworld_auto`                     | unit        | gray-mean estimate within 5 % on synthetic |
| 45 | `test_node_colormatrix` (extended)                | unit        | upgraded path with interpolated matrix from metadata |
| 46 | `test_node_denoise_bm3d`                          | unit        | RMSE reduction on synthetic noise; IPOL reference; sigma_override |
| 47 | `test_node_denoise_guided_filter`                 | unit        | edge preservation; cpipe self-reference |
| 48 | `test_node_denoise_wavelet_bayes_shrink`          | unit        | chroma denoise; scikit-image reference |
| 49 | `test_node_sharpen_edge_aware_usm`                | unit        | edge enhancement; RT 5.10 reference |
| 50 | `test_node_tone_filmic_rgb`                       | unit        | piecewise-poly continuity; cpipe self-reference |
| 51 | `test_node_tone_mertens_local`                    | unit        | Mertens fusion on synthetic stack; IPOL reference |
| 52 | `test_node_tone_aces_filmic`                      | unit        | Narkowicz fit on synthetic gradient |
| 53 | `test_node_tone_reinhard`                         | unit        | closed-form reference |
| 54 | `test_node_color_3d_lut`                          | unit        | tetrahedral apply on identity LUT + arbitrary `.cube` |
| 55 | `test_cube_3d_lut_loader`                         | unit        | `.cube` / `.spi3d` parsing |
| 56 | `test_node_fusion_hdr_plus_derivative`            | unit        | passthrough behavior; registry visibility |
| 57 | `test_ocio_config_v0_2`                           | unit        | 4 colorspaces / 4 displays / 2 looks load |
| 58 | `test_node_color_scene_linear_to_display`         | unit        | OCIO Vulkan compute path; sRGB / BT2020-PQ; UNSUPPORTED for DisplayP3 / HLG |
| 59 | `test_icc_v4_4_writer`                            | unit        | `lutAtoBfloatType` + `cicp` tag write + lcms2 sanity load |
| 60 | `test_output_heif_sdr` (extended)                 | unit        | pure-encoder contract |
| 61 | `test_output_heif_hdr_pq`                         | unit        | Main10 + CICP `(9,16,9)` + mdcv + clli + ICC v4.4 |
| 62 | `test_full_classic_pipeline_dng_to_heif_sdr`      | integration | full SDR DAG; Pixel 8 Pro + QBC fixtures; RT 5.10 reference PSNR ≥ 37 dB |
| 63 | `test_full_classic_pipeline_dng_to_heif_hdr`      | integration | full HDR DAG; cpipe self-reference Rec.2020-PQ; PSNR ≥ 37 dB |
| 64 | `test_golden_p2_nodes`                            | golden      | 18+ new per-node EXR golden pairs PSNR ≥ 40 dB |

| Label | Tests   | Purpose |
|-------|---------|---------|
| `unit` | 30–61   | per-component invariants |
| `integration` | 62, 63 | full pipeline behaviors |
| `golden` | 64 (18+ sections) | per-node golden PSNR ≥ 40 dB |
| `vulkan` | 31, 41 (Q6 variant), 58 | conditional on `CPIPE_VULKAN_AVAILABLE` |

P1's tests 13–29 all continue to pass; the diamond DAG test (P1 test 27) still gates concurrent dispatch.

---

## 10. Risk Register (P2-only)

| #     | Risk                                                                                                                       | Impact | Likelihood | Mitigation |
|-------|-----------------------------------------------------------------------------------------------------------------------------|--------|------------|------------|
| P2-R1 | No public Halide BM3D schedule; tuning consumes the budgeted 3 weeks (Research 07 OQ 2).                                    | High   | Medium     | T14 reads the BM3D-GPU CUDA reference schedule (BSD) before authoring the Halide schedule; validates PSNR on IPOL reference at each iteration. Schedule prototype lands behind `CPIPE_BM3D_PROTOTYPE` flag before integration. |
| P2-R2 | RawTherapee 5.10 installation on the development machine fails (newer Ubuntu, missing GTK build deps).                     | Medium | High       | T1 (CI bootstrap) verifies RT 5.10 AppImage works; fallback is the AppImage path; goldens land via Git LFS once authored locally so CI never depends on RT. |
| P2-R3 | OCIO 2.4 Vulkan GPU shader codegen (`OCIO::GPU_LANGUAGE_VK_GLSL_4_50`) emits GLSL that glslang cannot compile cleanly on RTX driver. | Medium | Low        | T19 prototype with a synthetic config first; fallback is to compile OCIO GLSL to GLSL_450 → SPIR-V externally, ship the SPIR-V alongside `config.ocio` v0.2. |
| P2-R4 | Pixel 8 Pro 50 MP Pro-mode QBC DNG is not available on raw.pixls.us under CC0; alternative sources are not cleanly licensed. | Medium | Medium     | T8 starts with a synthetic 4×4 QBC fixture and unit-test path; the cropped Pixel 8 Pro QBC DNG lands as soon as a CC0 / CC-BY source is identified; if no source materializes, demote to Samsung Galaxy S24 Ultra QBC sample and update P2-PD-37. |
| P2-R5 | kvazaar Main10 quality at `--preset medium` on natural images differs noticeably from x265 expectations (no CI gate per RD-15). | Medium | Low        | T20 records subjective notes on the SDR / HDR smoke runs; P3 microbench harness records the numbers; not a P2 ship-blocker per RD-15. |
| P2-R6 | 4×4 GainMap shape varies across Pixel 8 Pro firmware versions; cropped fixture may not exercise the 4-plane path. | Medium | Medium     | T9 tests both 1-plane and 4-plane via synthetic + the real Pixel 8 Pro QBC fixture; if the fixture is 1-plane, T9 also synthesizes a 4-plane fixture from the same DNG by replication. |
| P2-R7 | Handwritten ICC v4.4 `lutAtoBfloatType` writer (P2-PD-29) emits bytes that some readers reject (lcms strict mode, Photoshop). | High   | Medium     | T20 round-trips through lcms 2.16; the developer manually verifies the HDR HEIF in macOS Preview (or Photoshop CC if available); fallback is to drop the ICC v4.4 and ship CICP-only (P2-PD-29 alt path), recorded as a slip. |

Inherited risks ([Research 00 §7](research/00-summary.md#7-risk-register)) carried into P2: **R7** (Quad Bayer remosaic-then-demosaic vs direct quality — accepted), **R12** (TaskFlow + Vulkan integration — first exercised end-to-end here via T2 + T19), **R13** (JPEG XL DNG — v1 read-only).

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

# 6. Per-node golden (18+ new nodes + carried, PSNR ≥ 40 dB)
ctest --preset linux-release-clang -L golden --output-on-failure

# 7. SDR integration smoke (Pixel 8 Pro Bayer → SDR HEIF, RT 5.10 reference PSNR ≥ 37 dB)
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro.dng \
    -p examples/pipelines/full-classic-pipeline.cpipe.json \
    -o /tmp/cpipe_p2_sdr.heif
heif-info /tmp/cpipe_p2_sdr.heif | grep -E "color profile|nclx 1, 13|nclx 1,13"

# 8. HDR integration smoke (Pixel 8 Pro Bayer → HDR PQ HEIF)
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro.dng \
    -p examples/pipelines/full-classic-pipeline-hdr.cpipe.json \
    -o /tmp/cpipe_p2_hdr.heif
heif-info /tmp/cpipe_p2_hdr.heif | grep -E "Main10|nclx 9, 16|mdcv|clli"

# 9. Quad Bayer smoke (Pixel 8 Pro QBC → SDR HEIF, no crash)
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro-qbc.dng \
    -p examples/pipelines/full-classic-pipeline.cpipe.json \
    -o /tmp/cpipe_p2_qbc.heif
heif-info /tmp/cpipe_p2_qbc.heif

# 10. CI status
gh run list --workflow=build-and-test.yml --branch=main --limit=5
#    => All five most-recent runs on main must be "completed success".

# 11. Tag
git tag -a v0.3 -m "cpipe v0.3 — Classic Nodes + HDR"
git push origin v0.3

# 12. GitHub Release
gh release create v0.3 --verify-tag --generate-notes --title "cpipe v0.3 - Classic Nodes + HDR"
```

If commands 1–12 all return zero exit status and latest `main` CI is green on the release-candidate commit, P2 is done. The 24-hour bake remains waived per P2-PD-4 (carries P1-PD-4 / P0-PD-37).

---

## 12. What Shipped / What Slipped

> Authored by the closing PR (T21). Update `roadmap.md` §5 and `README.md` "Current Status" in the same commit.

> *Placeholder — filled in at T21 close. Expected shape: enumerate the 18+ shipped P2 nodes + planner / suite upgrades + HDR PQ path + OCIO Looks; record any algorithm degradations or fixture procurement deviations against the PD table.*

---

## 13. Dependencies (vcpkg.json + FetchContent additions)

No new vcpkg dependencies in P2. The P1 manifest (libraw, libheif[kvazaar], libde265, kvazaar overlay, opencolorio, lcms, vulkan-memory-allocator, vulkan-headers, vulkan-loader, openimageio, tracy) covers P2 in full.

Lensfun (LGPL3) — declined for v1; deferred to v1.1 per [P2-PD scope decision in this round](#4-phase-decisions-p2-pd-n).

Halide v21 stays on FetchContent (P0-PD-31 / P1-PD-7); P2-PD-59 records that the planned `HL_VULKAN_NO_HOST_RUNTIME` build-system change is not available in the local Halide v21 package.

---

## 14. Open Questions

P2 carries no globally-new open questions. The following items are implicitly open inside P2 and tracked locally; if any of them surfaces as a hard blocker, escalate by adding a new P2-PD row in this file.

- **kvazaar Main10 vs x265 quality on natural HDR images.** Not pre-verified; T20 records subjective notes. P3 microbench harness will quantify (Research 14 OQ 1).
- **Apple Photos / macOS Preview rendering of the cpipe-emitted HDR HEIF.** Round-trip is verified via libheif + libde265 on Linux, but Apple readers may treat the ICC v4.4 + CICP combination differently. T20 tests on macOS if a developer has access; otherwise recorded as a follow-up.
- **OCIO config v0.2 looks fidelity.** The two `Standard SDR` / `Standard HDR` looks are simple FP16 1D-LUTs in P2; the user-grade ACES 2.0 Output Transform fidelity is P3+ work, not exposed in v0.3.
- **BM3D `sigma_override` interaction with NoiseProfile.** When both are present, `sigma_override` wins (per T14 design). Whether this is the right default UX is a P3 editor topic.

The following [Architecture §17](architecture.md#17-open-questions) global open questions are touched by P2:

- **Q6** (Quad Bayer GainMap multi-plane) — **Resolved in P2-PD-18** (T9 implementation + golden).

The 10 remaining global open questions (Q2, Q3, Q4, Q5, Q7, Q8, Q9, Q11, Q13, Q14) are not in P2 scope; they remain tracked in [architecture.md §17](architecture.md#17-open-questions).

---

## 15. Out of Scope (P2)

Stated explicitly so contributors don't accidentally expand P2:

- AI ISP nodes (NAFNet, AdaInt, Wronski) — P4.
- Web Editor (React Flow), Editor server, offline JSON mode — P3.
- IQA harness, 50-image corpus, microbench harness, IQA / perf dashboards — P3.
- `cpipe info / serve / bench / iqa / model` CLI verbs — P3+ (P2-PD-39).
- HLG / UltraHDR / Apple Adaptive HDR output — v1.1 (RD-7).
- WSS / LNA / WebRTC / TURN editor connectivity — v1.1 (RD-8).
- ExecuTorch + ONNX Runtime + QAIRT integration — P4.
- Camera2 / Android target — v1.1.
- Adobe DNG SDK ingest — cancelled (RD-11).
- Hexagon / Metal device planes — v1.1 / v2.
- Direct 4×4 Quad Bayer demosaic (AI or classic) — v1.2.
- X-Trans demosaic — D12 / v1.2.
- Lensfun lens corrections — v1.1.
- 12-bit HEVC Main12 HEIF — v2 (requires non-kvazaar encoder).
- NVENC HEVC encoder — v1.1+ / v2.
- Handwritten SPIR-V dispatch ops (i.e. non-Halide GPU nodes) — P5+ if needed.
- JPEG output / JPEG fallback — D17 forbids (RD-17).
- DNG re-export (round-tripping OpcodeList raw bytes back out) — v2.
- Vendor-specific OpcodeList opcodes (Pixel-internal etc.) — v2.
- macOS / Windows / iOS targets — v1.2+ / v2.
- Editor-side authoring of new node types — Q15 resolved no (v1).
- External `.so` plugin loading — D4 reservation; v2.
- Mertens HDR-pyramid variant (HDR-aware exposure-fusion) — v1.1 (Research 07 OQ 6).
- AMaZE GPU AOT — P3+ (P2 ships CPU only per P2-PD-50).
- BM3D GPU AOT — P3+ (P2 ships CPU only per P2-PD-50).
- ACES 2.0 Output Transform fidelity (cg-config-v4.0.0_aces-v2.0_ocio-v2.5) — P3+ via OCIO config update.

---

## 16. See Also

- [`roadmap.md`](roadmap.md) §5 — P2 phase row + RD-NN decisions.
- [`phase-01-walking-skeleton.md`](phase-01-walking-skeleton.md) — preceding phase doc.
- [`phase-00-foundation.md`](phase-00-foundation.md) — initial repo / CI / ABI skeleton.
- [`architecture.md`](architecture.md) — six-target layout, threading model, lifecycle; §17 Q6 resolved here.
- [`buffer.md`](buffer.md) — `BufferMetadata` (P1 carries; P2 uses ext_blobs for OpcodeList1/2/3 raw bytes).
- [`plugin-sdk.md`](plugin-sdk.md) §3 — `cpipe_compute_suite_v1` (tail-extended in P2-PD-5 / P2-PD-6).
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — full scheduler design (P2 implements the interference-graph subset).
- [`research/07-classic-isp-algorithms.md`](research/07-classic-isp-algorithms.md) — 18 classic nodes; reference papers for RCD / AMaZE / BM3D / Mertens / 3D-LUT / etc.
- [`research/12-dng-format.md`](research/12-dng-format.md) — basis for OpcodeList2 / OpcodeList3 dispatchers and Quad Bayer.
- [`research/13-color-management.md`](research/13-color-management.md) — basis for WB / colormatrix upgrade, OCIO config, ICC v4.4 writer.
- [`research/14-heif-and-hdr-output.md`](research/14-heif-and-hdr-output.md) — basis for output.heif_hdr_pq + kvazaar Main10 + mdcv / clli.
