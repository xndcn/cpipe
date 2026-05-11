# cpipe Roadmap

> Date: 2026-05-11 · Companion: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`research/00-summary.md`](research/00-summary.md)

This is the **project management master plan** for cpipe v1.0 and the high-level outlook for v1.1, v1.2, and v2. It defines what v1.0 GA contains, what each phase produces, how releases are tagged, how risks are tracked across phases, and what is explicitly out of scope.

Roadmap is the **coarse view**. Per-phase `docs/phase-XX-*.md` files (written at the start of each phase) carry the detailed task lists. Architectural detail lives in [`architecture.md`](architecture.md); evidence lives in `docs/research/`. This file does not duplicate that material — it sequences it.

---

## 1. Decision Quick Reference

Every roadmap-level decision is locked here so contributors can answer the common "why is X structured this way" questions without re-litigating the design. Numbered RD-NN ("Roadmap Decision") tags are stable; subsequent docs may cite them.

| ID    | Decision                              | Value                                                                                                                                              |
|-------|---------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------|
| RD-1  | Roadmap role                          | **Project management master plan** — phase objectives, DoD, acceptance gates, decision points, links to per-phase detail docs.                     |
| RD-2  | v1.0 GA minimum deliverable           | **Linux CLI + Web Editor**. Android slides to v1.1 (see [Slip Absorption](#7-slip-absorption-and-scope-pressure-valves)).                            |
| RD-3  | Team model assumption                 | **Single maintainer + AI pair (Claude / Codex)**. All phases are sequential; cross-phase parallelism only when scoped per-task.                    |
| RD-4  | Timeline                              | **Not time-boxed**. Quality and decision correctness drive cadence. Per-phase ETA may appear in `phase-XX.md`; this file does not commit dates.    |
| RD-5  | Classic ISP node coverage in v1.0     | **All 18 nodes** from [Research 07](research/07-classic-isp-algorithms.md) ship in v1.0.                                                            |
| RD-6  | AI node coverage in v1.0              | **All 3 AI nodes** (NAFNet-w32 denoise, AdaInt 3D-LUT, HDR+ Wronski burst neural denoise) ship in v1.0.                                            |
| RD-7  | HDR output formats in v1.0            | **SDR HEIF + HDR HEIF (PQ only)**. HLG, UltraHDR, Apple Adaptive HDR slip to v1.1.                                                                  |
| RD-8  | Editor connectivity in v1.0           | **Plaintext HTTP on loopback / LAN only** (no TLS, no WSS, no LNA, no WebRTC, no TURN). Modern-browser mixed-content limitations are accepted.     |
| RD-9  | Quad Bayer handling in v1.0           | **Remosaic-to-2×2 path only** ([X3](research/00-summary.md#5-cross-cluster-decision-matrix)). Native 4×4 demosaic deferred to v1.2 with AI demosaic. |
| RD-10 | Web Editor offline (Q8)               | **Provide a pure-local-JSON mode** (File System Access API + localStorage). The editor reads / writes `pipeline.cpipe.json` without a runtime.    |
| RD-11 | Adobe DNG SDK review (Q1)             | **Cancelled.** v1 ingest path locks to **LibRaw 0.22 + first-party OpcodeList interpreter**. Adobe path re-evaluated post-v1 only if needed.        |
| RD-12 | Color management scope in v1.0        | **ICC embed + CICP signalling + OCIO Looks menu** (ACES / sRGB looks). Working space stays linear Rec.2020 D65.                                    |
| RD-13 | IQA gate at v1.0 GA                   | **Per-node golden image only** (EXR fixtures, PSNR ≥ 40 dB vs reference). 50-image corpus is recorded-not-gated until v1.1.                        |
| RD-14 | Camera coverage at v1.0 GA            | **"Any DNG 1.4+" + 5–6 representative-model golden fixtures** (Pixel 8 Pro, iPhone 16 Pro, Galaxy S24 Ultra, one Sony A7, one Fuji X-T, one Canon R). |
| RD-15 | Performance gate at v1.0 GA           | **Recorded, not gated.** `bench/results/<commit>.json` accumulates latency / memory data; CI never rejects on regression in v1.                     |
| RD-16 | Release cadence                       | **`v0.1 → v0.2 → v0.3 → v0.4 → v0.5 → v1.0-rc1 → v1.0`** — one tag per main phase plus an RC.                                                       |
| RD-17 | Repository visibility                 | **Public on GitHub from P0**. README clearly marks pre-v1 builds as alpha.                                                                          |
| RD-18 | Beta recruitment                      | **None in v0.x**. GitHub Issues only; no organized beta cohort before v1.0 GA.                                                                      |
| RD-19 | Phase granularity in this file        | **6 main phases + internal sub-domains**. Detail per phase lives in `docs/phase-0X-*.md` (one file per phase, written at phase start).             |
| RD-20 | Phase-XX docs cadence                 | `phase-0X-*.md` is written **at the start of each phase** (1–2 week ramp), then maintained through the phase. Not written upfront.                 |
| RD-21 | Development hardware                  | **Linux x86_64 + NVIDIA RTX + Vulkan 1.3** as primary; AMD/Intel Mesa is best-effort.                                                              |
| RD-22 | PR quality gate                       | `clang-format` + `clang-tidy` + Catch2 unit tests + ASAN/UBSAN on Debug.                                                                            |
| RD-23 | License audit policy                  | **Manual at PR review** when dependencies are added/changed. No CI license-check / SPDX scan in v1.                                                |
| RD-24 | ADR practice                          | **Not used in v1.** D1–D19 / B1–B12 / P1–P16 stay where they are (research/_toc.md, buffer.md, plugin-sdk.md). RD-NN tags in this file are the equivalent. |
| RD-25 | Slip / rollback strategy              | **Negotiated when triggered.** No pre-committed cut list — the maintainer decides per incident, recorded in the active phase doc.                  |
| RD-26 | AI model weights for v1.0             | **Pre-trained weights from upstream (NAFNet GitHub MIT, AdaInt BSD, Wronski re-implementation from paper).** No fine-tuning or training infra in v1.0. |
| RD-27 | v1.1 contents                         | **Android + remaining HDR (HLG / UltraHDR / Apple Adaptive HDR) + remaining editor-connectivity tiers (WSS-LE / LNA / WebRTC+CF / TURN).**           |
| RD-28 | Roadmap doc language                  | **English** (consistent with the rest of `docs/`).                                                                                                  |

This table is the **single source of truth** for roadmap-level questions. Architectural / library / algorithm questions still belong to `architecture.md` / `tech.md` / `research/`.

---

## 2. Roadmap Shape

```
   P0 Foundation     P1 Walking Skeleton    P2 Classic + HDR    P3 Editor + IQA    P4 AI Nodes     P5 Polish
   ───────────────   ───────────────────    ─────────────────   ───────────────    ────────────    ──────────
   skeleton repo  ▶  Linux DNG→SDR HEIF  ▶  18 nodes + HDR PQ ▶ web editor + IQA ▶ 3 AI nodes  ▶  v1.0 GA
   v0.1              v0.2                   v0.3                v0.4               v0.5            v1.0-rc1, v1.0
```

Six main phases, sequential. Sub-domains inside a phase may be tackled in any internal order at the maintainer's discretion. Each phase ends with a tag and a phase-XX writeup that records what shipped, what slipped, and what carried into the next phase.

Each subsequent section follows the same template:

- **Objective** — one-line outcome.
- **Sub-domains** — coarse work areas; the phase doc breaks these into tasks.
- **Inputs** — pre-conditions / artefacts from prior phases.
- **Outputs** — concrete artefacts produced.
- **Definition of Done (DoD)** — what must be true to tag and move on.
- **Acceptance gate** — verifiable check (test run, sample command, etc.).
- **Active risks** — references to `research/00-summary.md §7` R-numbers; this file does not restate.
- **Decision points** — RD-NN locks consumed; lookahead to v1.1 decisions deferred (no live Q's open against v1.0 by phase entry).

---

## 3. Phase 0 — Foundation (tag `v0.1`)

**Objective.** A repository skeleton that compiles, tests, and runs a trivial passthrough node end-to-end on Linux.

**Sub-domains.**

- Repo skeleton, license headers, `LICENSE`, `CONTRIBUTING.md`, public GitHub repo.
- CMake top-level + presets; vcpkg manifest with baseline pin; FetchContent for Halide and Slang.
- CI on GitHub Actions: Linux x86_64 Debug + Release builds; `clang-format` + `clang-tidy` + Catch2 + ASAN/UBSAN gates.
- `cpipe-core` static lib: `PixelFormat`, `BufferLayout`, `IBuffer` interface, status codes.
- `cpipe-runtime` static lib stub: `Pipeline` skeleton, `Scheduler` skeleton, `ComputeContext` skeleton with Halide AOT integrated.
- TaskFlow v4.0.0 integrated; one-thread Executor smoke test.
- Plugin C ABI header (`cpipe/sdk/cpipe_node.h`) drafted per [`plugin-sdk.md`](plugin-sdk.md); not yet exercised by external nodes.
- `cpipe-builtin-nodes` static lib with one trivial passthrough node registered via `CPIPE_REGISTER_NODE`.
- `apps/cli/` minimal `cpipe run` entry point: loads a 1-node JSON pipeline, invokes Pipeline, exits.

**Inputs.** None.

**Outputs.** Public GitHub repo, green CI matrix, one passing end-to-end unit test (`tests/unit/test_passthrough.cpp`).

**Definition of Done.**

1. `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset ci` is green.
2. `cpipe run tests/fixtures/passthrough.dng -p tests/fixtures/passthrough.json -o /tmp/out.bin` exits 0 and produces a non-empty file.
3. Repository is public, README marks "Pre-alpha — APIs unstable", `LICENSE` is Apache 2.0.

**Acceptance gate.** CI workflow `linux-x86_64-build-and-test` passes on `main`. Single passthrough integration test is the smoke test.

**Active risks.** None new. Establish the CI matrix that subsequent phases rely on for [R12](research/00-summary.md#7-risk-register) (TaskFlow + Vulkan integration) visibility.

**Decision points.** RD-1 through RD-28 are all locked entering this phase. No deferred decisions consumed.

---

## 4. Phase 1 — Walking Skeleton (tag `v0.2`)

**Objective.** A real DNG (Bayer, no Quad Bayer yet) becomes an SDR HEIF through 5 stages of the canonical ISP pipeline on Linux, end-to-end on a developer laptop.

**Sub-domains.**

- DNG ingest: LibRaw 0.22 + first-party OpcodeList interpreter (`cpipe::ingest::dng_opcode`) reading OpcodeList1 / LinearizationTable / OpcodeList2 / black/white-level / CFA inspection.
- 5 nodes: `linearize`, `blacklevel`, `demosaic.rcd_or_bilinear` (Bayer only — bilinear acceptable initially), `wb.dual_illuminant`, `colormatrix.dng_to_working`.
- Working color space wiring: linear Rec.2020 D65; OCIO 2.4 + lcms2 2.16 integrated.
- HEIF output node (SDR only): libheif 1.20.1 + kvazaar 2.3; ICC + CICP signalling.
- Scheduler: TaskFlow-driven topo dispatch with memory planner (interference-graph coloring) and precision-planner stub.
- Tracy tracing compiled in behind `CPIPE_ENABLE_TRACY`.
- Vulkan device plane minimum: Halide Vulkan AOT path for at least one of the 5 nodes; CPU fallback for the rest.
- `tests/golden/` directory created (Git LFS bootstrapped); golden fixtures for the 5 nodes.

**Inputs.** P0 outputs (skeleton, CI, plugin ABI header).

**Outputs.** A working `cpipe run input.dng -p min-pipeline.json -o out.heif` for a real Pixel 8 Pro DNG. Tag `v0.2`.

**Definition of Done.**

1. Process a Pixel 8 Pro Bayer DNG → SDR HEIF; viewer opens the result.
2. PSNR vs a reference processing (RawTherapee-equivalent output, hand-validated) within 3 dB at the 5-node stage.
3. Golden image fixtures land for all 5 nodes; `ctest --preset golden` is green.

**Acceptance gate.** Smoke test in CI: process `tests/corpus/pixel8pro.dng` → `out.heif`, compare against pinned golden, fail if PSNR < 40 dB.

**Active risks.** [R11](research/00-summary.md#7-risk-register) (libheif LGPL) — confirm dynamic-link path works on Linux. [R13](research/00-summary.md#7-risk-register) (JPEG XL DNG) — accept v1 reads only.

**Decision points.** [Q12](research/00-summary.md#9-consolidated-open-questions) (Windows v1) already resolved — not in v1. No new decisions opened.

---

## 5. Phase 2 — Classic Nodes + HDR (tag `v0.3`)

**Objective.** All 18 classic nodes from [Research 07](research/07-classic-isp-algorithms.md) ship; SDR + HDR (PQ) HEIF output is fully wired; OCIO Looks menu is operational; Quad Bayer remosaic path lands.

**Sub-domains.**

- **Demosaic family**: `demosaic.rcd` (re-implemented from primary paper — license-clean), `demosaic.amaze` (re-implemented from primary paper), `demosaic.bilinear` (fallback).
- **White balance**: `wb.dual_illuminant` (full implementation: ColorMatrix1/2 + ForwardMatrix1/2 + AsShotNeutral interpolation), `wb.greyworld_auto` (helper).
- **Tone mapping**: `tone.filmic_rgb` (primary global), `tone.mertens_local` (Laplacian exposure-fusion local tone), `tone.aces_filmic` (ICC-friendly alternate), `tone.reinhard` (debug fallback).
- **Denoise**: `denoise.bm3d` (re-implemented from primary paper — primary high-quality), `denoise.guided_filter` (fast preview), `denoise.wavelet_bayes_shrink` (chroma).
- **Sharpen**: `sharpen.edge_aware_usm` (guided-filter-based USM).
- **Lens correction**: `lens.dng_opcode_list_3` (full OpcodeList3 dispatcher: GainMap, WarpRectilinear, FixVignetteRadial, FixBadPixelsConstant, FixBadPixelsList, TrimBounds).
- **Color**: `color.3d_lut` (3D-LUT applier; works with OCIO Looks).
- **Multi-frame fusion (single-input variant only in P2)**: `fusion.hdr_plus_derivative` placeholder (architecture present; full burst processing waits for P4 multi-frame `BatchedBuffer`).
- **Quad Bayer**: `demosaic.quad_bayer_remosaic` (4×4 → 2×2 Bayer remosaic-then-RCD path).
- **Output**: `output.heif_sdr` (Phase 1 carry-over), `output.heif_hdr_pq` (CICP `9 / 16 / 9`, BT.2100-PQ); ICC profile embed retained.
- **OCIO Looks**: load OCIO config bundled with cpipe (`share/cpipe/ocio/`), expose Look menu in the editor.

**Inputs.** P1 outputs (5-node skeleton + LibRaw + DNG opcode interpreter + golden test harness).

**Outputs.** Tag `v0.3`. 18 classic-node binaries + golden fixtures + an `examples/pipelines/full-classic-pipeline.cpipe.json` reference graph.

**Definition of Done.**

1. All 18 nodes ship with golden EXR fixtures and pass golden PSNR ≥ 40 dB.
2. SDR HEIF + HDR HEIF (PQ) output paths both produce viewable images on an HDR-capable monitor (subjective).
3. Quad Bayer DNG (Pixel 8 Pro full-resolution Pro-mode DNG, if obtainable, or a Sony / Samsung sample) processes through the remosaic path without crash; result subjectively acceptable.
4. OCIO Looks menu wired and at least 2 looks (Standard SDR / Standard HDR) load correctly.

**Acceptance gate.** `examples/pipelines/full-classic-pipeline.cpipe.json` produces both SDR and HDR HEIF outputs from `tests/corpus/pixel8pro.dng` in CI. Golden image diff against the pinned reference.

**Active risks.** [R4](research/00-summary.md#7-risk-register) (libheif gain-map — irrelevant in v1.0 since UltraHDR slipped, but architectural shape is exercised). [R7](research/00-summary.md#7-risk-register) (Quad Bayer remosaic-then-demosaic quality vs direct demosaic) — accepted: direct 4×4 deferred to v1.2.

**Decision points.** None deferred to user — all v1.0 scope is in RD-1..RD-28.

---

## 6. Phase 3 — Editor + Quality Harness (tag `v0.4`)

**Objective.** Web Editor renders the DAG, edits parameters live against a running runtime, and a quality harness (golden + 50-image corpus) is in place — gated as recorded-not-blocked at GA.

**Sub-domains.**

- **Web Editor (`apps/web/`)**: Vite + React 19 + TypeScript + React Flow 12 + Zustand + Ajv. Custom node component with manifest-driven parameter forms. Pipeline graph load / save.
- **Offline JSON mode** (RD-10): File System Access API + localStorage; editor opens / edits `pipeline.cpipe.json` without a runtime.
- **Editor server (`cpipe-server`)**: uWebSockets-based HTTP server inside the cpipe runtime. **Plaintext HTTP on loopback / LAN only** (RD-8). REST endpoints from [`architecture.md` §8](architecture.md#8-editor-server-surface): `/api/health`, `/api/registry/nodes`, `/api/pipelines/:id`, `/api/pipelines/:id/params`, `/api/pipelines/:id/run`.
- **WebSocket event plane**: framed binary thumbnail (256×256 WebP) + profile event stream. No pairing crypto (no Noise XK in v1.0 — recorded as v1.1 work).
- **GitHub Pages deploy**: `gh-pages` branch, GitHub Actions workflow on `apps/web/**` changes.
- **IQA harness (`tests/iqa/`)**: piq in-binary for PSNR / SSIM / MS-SSIM / ΔE2000; pyiqa subprocess for license-isolated metrics.
- **50-image v1 corpus**: curated subset of FiveK + SIDD + DND + HDR+ + RAISE + Kalantari + Quad Bayer phone shots ([Research 09](research/09-image-quality-benchmarks.md)). Stored under `tests/corpus/` with Git LFS. Wilcoxon-signed-rank corpus check implemented but **recorded-not-gated** (RD-13).
- **Microbenchmark harness (`bench/`)**: Google Benchmark + nanobench scaffolding. Results recorded to `bench/results/<commit>.json`; CI does not reject on regression (RD-15).
- **Dashboard**: Vega-Lite IQA + perf dashboard published to a `gh-pages` branch path alongside the editor.

**Inputs.** P2 outputs (18 nodes producing meaningful images; manifest schema stable).

**Outputs.** Tag `v0.4`. Editor accessible at `https://<user>.github.io/cpipe/editor/`; pairs with a local runtime via `http://localhost:4747`. IQA + perf dashboard at `https://<user>.github.io/cpipe/dashboard/`. 50-image corpus committed.

**Definition of Done.**

1. Editor renders the example 18-node pipeline DAG; the user can change a parameter (e.g. `tone.filmic_rgb.ev`) and see the resulting HEIF re-rendered.
2. Pure-local-JSON mode opens and saves `pipeline.cpipe.json` without a runtime (RD-10).
3. IQA corpus run completes for the example pipeline and posts results to the dashboard.
4. Microbench suite runs and records baseline numbers for the 18 classic nodes on the development NVIDIA RTX machine.

**Acceptance gate.** Editor → runtime → IQA dashboard end-to-end smoke test in CI (headless browser hitting a local `cpipe serve`).

**Active risks.** [R9](research/00-summary.md#7-risk-register) (Chrome 142 LNA UX) — irrelevant in v1.0 since we ship plaintext HTTP only. [R15](research/00-summary.md#7-risk-register) (pyiqa CC-BY-NC-SA) — already mitigated by subprocess isolation.

**Decision points.** None deferred.

---

## 7. Phase 4 — AI Nodes (tag `v0.5`)

**Objective.** Three AI ISP nodes (NAFNet-w32 denoise, AdaInt 3D-LUT, HDR+ Wronski burst neural denoise) run on the Linux desktop with the multi-frame `BatchedBuffer` data model in place.

**Sub-domains.**

- **Multi-frame `BatchedBuffer`**: extend `cpipe::IBuffer` to support cardinality-N inputs ([`buffer.md` §10.3](buffer.md#103-pipeline-inputs--outputs)).
- **HDR+ derivative align/merge/finish stages**: alignment pyramid + tile-merge with raised cosine, Bayer-domain. Not gated on neural denoise — classical path lands first.
- **ExecuTorch 1.2.0 integration**: XNNPACK CPU backend in `cpipe-runtime`. Linux desktop only.
- **ExecuTorch Vulkan backend**: AOT-compile NAFNet-w32 + AdaInt 3D-LUT.
- **ONNX Runtime 1.25.1 escape hatch**: load `.onnx` models when ExecuTorch path fails for a given node.
- **AI nodes**:
  - `ai.denoise.nafnet_w32` — pre-trained weights from official NAFNet GitHub repo (MIT); no fine-tuning (RD-26).
  - `ai.color.adaint_3dlut` — pre-trained weights from official AdaInt repo (BSD); no fine-tuning.
  - `ai.burst_denoise.wronski` — re-implemented from primary paper; weights from author release if available, otherwise marked "experimental" and gated behind a config flag.
- **Model fetch / verify**: `scripts/fetch_models.py` downloads and SHA256-verifies model artefacts into `models/` (gitignored; manifest committed).
- **Quantization calibration** for INT8 paths on CPU/GPU (NPU path is v1.1 Android work). FiveK + SIDD reference set used; ≤ 0.4 dB SIDD PSNR drop is the gate per [R8](research/00-summary.md#7-risk-register).
- **Golden fixtures** for AI nodes use FP16 reference outputs.

**Inputs.** P3 outputs (editor lets you toggle AI nodes; IQA harness verifies them).

**Outputs.** Tag `v0.5`. AI denoise + AI 3D-LUT + Wronski burst nodes running on desktop GPU. Multi-frame burst processing infrastructure in place.

**Definition of Done.**

1. End-to-end pipeline with AI denoise + AI 3D-LUT + classical HDR+ fusion runs on Linux desktop; produces a result within ±5 % of published IQA benchmarks on the SIDD test set ([Research 09](research/09-image-quality-benchmarks.md)).
2. Wronski burst node runs (even if "experimental") on a 5-frame synthetic burst.
3. `scripts/fetch_models.py list / verify` works; model SHA256 manifest committed.
4. INT8 quantization calibration script for NAFNet exists and produces a calibrated model that meets the [R8](research/00-summary.md#7-risk-register) tolerance on CPU/GPU.

**Acceptance gate.** IQA corpus run including AI nodes produces a recorded dashboard entry. Golden image diffs for the 3 AI nodes pass.

**Active risks.** [R8](research/00-summary.md#7-risk-register) (NAFNet INT8 quantization) — handled inside this phase (RD answer). [R1](research/00-summary.md#7-risk-register) / [R2](research/00-summary.md#7-risk-register) (Halide Hexagon / slang-rhi Metal) — not exercised in v1.0; assumption verified at v1.1.

**Decision points.** Wronski burst classification ("experimental" vs "stable") decided inside this phase based on weights availability.

---

## 8. Phase 5 — Polish (tags `v1.0-rc1`, `v1.0`)

**Objective.** Lock the v1.0 GA delta — documentation, sample plugin, golden + corpus refresh, license audit, RC bake — then tag `v1.0`.

**Sub-domains.**

- **Documentation pass**: every public type in `cpipe-sdk` has a doc comment; `docs/guides/` has authoring guides for new nodes; `docs/node-reference/` is generated from manifests.
- **Sample plugin** in `cpipe-sdk` style (a no-op or trivially-useful node) that demonstrates `CPIPE_REGISTER_NODE`, manifest authoring, golden fixture authoring.
- **Golden fixture refresh**: re-run golden tests across the 18 + 3 nodes against the final pinned versions of LibRaw, Halide, ExecuTorch, libheif.
- **5–6 representative-model golden fixtures**: Pixel 8 Pro, iPhone 16 Pro, Galaxy S24 Ultra, Sony A7, Fuji X-T, Canon R (RD-14).
- **License audit final pass** (manual per RD-23): every dependency in `tech.md` rechecked; license inventory in `research/00-summary.md §6` confirmed.
- **Performance numbers** published to the dashboard: corpus-wide latency / memory; recorded-not-gated per RD-15.
- **RC1 → public Issue triage**: tag `v1.0-rc1`, leave for 2–4 weeks of stabilization through GitHub Issues, fix release blockers.
- **GA**: tag `v1.0`.

**Inputs.** P4 outputs (full v1.0 scope code-complete).

**Outputs.** Tags `v1.0-rc1` and `v1.0`. Published documentation, IQA dashboard, and perf dashboard.

**Definition of Done — RC1.**

1. All 5–6 representative-model golden fixtures green; no golden regression in CI.
2. Documentation builds (mdBook / static site as decided in phase doc); links resolve.
3. Sample plugin compiles, registers, and runs via the same `CPIPE_REGISTER_NODE` macro used for built-in nodes.
4. License audit table in `tech.md` and `research/00-summary.md §6` reviewed and approved.

**Definition of Done — v1.0 GA.**

1. Zero open release-blocker Issues (defined as "crashes or wrong output on any of the 5–6 representative models").
2. Zero failing CI jobs for ≥ 7 consecutive days on `main`.
3. README, CHANGELOG, and release notes published.
4. v1.0 tag is signed (per `tech.md §14` "signed binary uploads") and the corresponding release attaches Linux x86_64 binaries and source tarballs.

**Acceptance gate.** "Definition of v1.0 GA" above is the gate. The maintainer is the sole reviewer (RD-3) and uses the checklist as the bar.

**Active risks.** No new risks introduced. Inherited risks from prior phases are either closed (R6 cancelled, R8 mitigated in P4) or deferred (R1, R2, R3, R4, R5, R10, R14 all v1.1+).

**Decision points.** Any slip required to ship v1.0 is decided live (RD-25); the maintainer records the cut in the phase doc.

---

## 9. Slip Absorption and Scope Pressure Valves

Per RD-25, slip / scope cuts are negotiated when triggered, not pre-committed. But the **shape** of the pressure valve is fixed so the maintainer is not improvising under deadline pressure:

1. **Slip the phase, not the scope** is the default. The roadmap is not time-boxed (RD-4); the right answer when a phase is at risk is usually "spend another month on the phase" before "cut something from v1.0".
2. **If a scope cut is required**, candidate cuts in roughly-this-order are *suggestions only*, recorded here for orientation; the active phase doc records the actual decision and rationale:
   - Wronski burst neural denoise → ship as "experimental" flag-gated instead of a first-class node.
   - HDR HEIF (PQ) → ship SDR HEIF only.
   - One denoise variant (BM3D or wavelet-BayesShrink) → ship the other.
   - 5–6 representative-model goldens → ship 3–4 instead.
3. **Hard floor for v1.0 GA**: Linux CLI + Web Editor + SDR HEIF + at least 12 classic nodes + at least 1 AI node + golden image harness. Anything less is not v1.0 — bump to v1.1.

These are *guidance*, not commitments. RD-25 governs.

---

## 10. v1.0 GA Definition (consolidated)

This section consolidates everything in the table at top into a single "ship checklist". The maintainer can use this directly at RC bake time.

**Functional surface.**

- [ ] Linux x86_64 CLI: `cpipe run`, `cpipe info`, `cpipe bench`, `cpipe serve`, `cpipe model`, `cpipe iqa`.
- [ ] Web Editor on GitHub Pages: graph view, parameter editing, offline JSON mode.
- [ ] 18 classic ISP nodes registered and golden-tested.
- [ ] 3 AI ISP nodes (NAFNet-w32 denoise, AdaInt 3D-LUT, Wronski burst — Wronski may be "experimental" per slip valve).
- [ ] DNG input via LibRaw + first-party OpcodeList interpreter.
- [ ] SDR HEIF + HDR HEIF (PQ) output.
- [ ] ICC profile embed + CICP signalling; OCIO Looks menu.
- [ ] Quad Bayer remosaic-to-2×2 path.
- [ ] Editor server: plaintext HTTP, loopback / LAN.

**Quality.**

- [ ] Golden image fixtures green for all 21 nodes.
- [ ] 5–6 representative-model goldens (Pixel 8 Pro, iPhone 16 Pro, Galaxy S24 Ultra, Sony A7, Fuji X-T, Canon R) pass.
- [ ] 50-image corpus run completes; dashboard updated; **not** a hard gate.
- [ ] Microbench run completes; numbers recorded; **not** a hard gate.

**Engineering.**

- [ ] CI green for ≥ 7 consecutive days on `main`.
- [ ] ASAN/UBSAN Debug builds clean.
- [ ] `clang-format` / `clang-tidy` clean.
- [ ] License audit reviewed; `tech.md` and `research/00-summary.md §6` accurate.

**Documentation.**

- [ ] README, CHANGELOG, release notes.
- [ ] Plugin authoring guide (`docs/guides/plugin-authoring.md`).
- [ ] Node reference (`docs/node-reference/*.md`, manifest-derived).
- [ ] Sample plugin source + golden fixtures.

**Release artefacts.**

- [ ] `v1.0` git tag (signed).
- [ ] GitHub Release with attached Linux x86_64 binaries + source tarball.
- [ ] Pages site updated: editor + dashboard.

---

## 11. Cross-Phase Concerns

A handful of activities cut across all phases; they are not phase outputs but should not be forgotten.

- **Issue triage**: GitHub Issues are the only feedback channel (RD-18). Triage ≥ weekly during active phases; tag with `phase-X` so the workload is visible.
- **Dependency hygiene**: every dependency added or bumped requires a manual license note in `tech.md` (RD-23). vcpkg baseline pinned per release tag.
- **Manifest schema stability**: `https://schemas.cpipe.dev/node/v0.1.json` is alpha. Breaking changes are allowed during v0.x; once `v1.0` ships, schema breaking changes follow plugin-sdk §4 suite-versioning rules.
- **`tech.md` and `architecture.md` drift**: these are the v1.0 contract surface. Any deviation discovered in implementation is fixed by updating those docs *and* the code in the same PR.
- **Roadmap maintenance**: this file is updated when an RD-NN locks. RD-NN tags are never re-numbered, even if a decision is reversed (a reversal is logged as a new RD-NN that cites the prior one).

---

## 12. v1.1 Outlook

v1.1 = **Android + remaining HDR + remaining editor-connectivity tiers** (RD-27). Coarse plan:

- **Android port**: `cpipe-android` target; AGP 8.7 + NDK r27 cross-compile; Compose UI + JNI; Camera2 NDK + AImageReader + `AHardwareBuffer` + `VK_ANDROID_external_memory_AHB` integration; custom DNG writer (not `DngCreator`); `Camera2BufferProducer`.
- **Hexagon HTP via QAIRT 2.43**: ExecuTorch QNN backend; NAFNet-w32 INT8 calibrated for Hexagon HTP V73 / V75 / V79; AHB → HTP memcpy boundary accepted per [R3](research/00-summary.md#7-risk-register).
- **Remaining HDR**: HLG, UltraHDR (libultrahdr 1.4), Apple Adaptive HDR best-effort write (read-only verified).
- **Editor connectivity full stack**: LAN-cert + WSS-LE, Chrome 142 LNA + WSS, WebRTC DataChannel + Cloudflare Workers signalling, Cloudflare TURN, Noise XK pairing.
- **Multi-camera burst** ([Q13](research/00-summary.md#9-consolidated-open-questions)) — decide entering v1.1.
- **Adobe DCP profile writer** ([Q4](research/00-summary.md#9-consolidated-open-questions)) — decide entering v1.1.

Open questions to resolve **before** entering v1.1 (per [Research 00 §9](research/00-summary.md#9-consolidated-open-questions)): Q2 (Qualcomm Vulkan-HTP interop), Q3 (HTP context-binary cache invalidation), Q6 (Quad Bayer GainMap multi-plane), Q9 (Apple Adaptive HDR validation strategy), Q13 (multi-camera burst v1 vs v2).

---

## 13. v1.2 Outlook

v1.2 = **macOS native target** (no MoltenVK per D18). Coarse plan:

- `cpipe-apple` target with native Metal 3 + IOSurface + `MTLHeap` allocator.
- Apple Core ML 8 + ANE via `MLProgram` for AI nodes (genuine zero-copy via IOSurface-backed CVPixelBuffer).
- macOS CLI parity with Linux CLI.
- AI demosaic (direct 4×4 Quad Bayer) — quality lift that v1.0 deferred ([Q7](research/00-summary.md#9-consolidated-open-questions)).
- Apple Adaptive HDR fidelity validation completed on macOS reader stack.

---

## 14. v2 Outlook

v2 opens the architecture beyond v1's hard limits:

- **External `.so` plugin loading** (D4 architectural reservation): the same `CPIPE_REGISTER_NODE` macro used for built-in v1 nodes will load external plugins via dlopen.
- **Plugin marketplace + signing model** ([Q11](research/00-summary.md#9-consolidated-open-questions)).
- **iOS** (Apple Core ML + ANE on iPhone / iPad; Compose-Multiplatform or SwiftUI UI shell — decided in v2 scoping).
- **MediaTek APU + Samsung NPU** ([Q14](research/00-summary.md#9-consolidated-open-questions)) — additional Hexagon-like backends.
- **Tile-based / out-of-core processing** (D2 reservation): for > 100 MP / 4×4-native Quad Bayer at full resolution.
- **Live preview / streaming pipeline** (D5 reservation): non-batch dataflow scheduler.
- **Zero-shutter-lag (ZSL) ring buffer** (D3 reservation).
- **Custom calibration capture** ([Q5](research/00-summary.md#9-consolidated-open-questions)) — chart-photography-to-`.cpcal` workflow; optional Adobe DCP writer ([Q4](research/00-summary.md#9-consolidated-open-questions)).
- **Editor in-browser node authoring** ([Q15](research/00-summary.md#9-consolidated-open-questions) resolved "no for v1"; v2 reopens).

---

## 15. Out of Scope Reminders (v1.0)

Reiterated here so contributors don't accidentally expand v1.0:

- Tile-based / out-of-core image processing (D2; [`buffer.md` §11](buffer.md#11-sub-view-not-implemented-in-v1)).
- Streaming / live preview (D5).
- Zero-shutter-lag (D3).
- Dynamic DAG topology (D6).
- External `.so` plugin loading (D4; [`plugin-sdk.md` §5.4](plugin-sdk.md#54-v2-external-so-loading)).
- Plugin sandboxing / marketplace / signing.
- macOS / iOS targets (timeline).
- Windows desktop target ([Q12](research/00-summary.md#9-consolidated-open-questions) resolved no).
- Android target (deferred to v1.1 per RD-2).
- In-browser node code authoring ([Q15](research/00-summary.md#9-consolidated-open-questions) resolved no).
- AI demosaic for Quad Bayer (deferred to v1.2 with direct 4×4 demosaic; v1.0 uses remosaic only per RD-9).
- X-Trans demosaic (D12).
- Adobe DCP profile *writer* ([Q4](research/00-summary.md#9-consolidated-open-questions); pending).
- HLG / UltraHDR / Apple Adaptive HDR output (slipped to v1.1 per RD-7).
- WSS, LNA, WebRTC, TURN editor connectivity (slipped to v1.1 per RD-8).
- Beta cohort recruitment (no, per RD-18).
- Performance regression CI gate (recorded-only per RD-15).
- AI model fine-tuning / training infrastructure (pre-trained weights only per RD-26).
- ADR file structure (not adopted in v1 per RD-24).

---

## 16. See Also

- [`architecture.md`](architecture.md) — system assembly: targets, threading, lifecycle.
- [`tech.md`](tech.md) — every library + version pin + license verdict.
- [`buffer.md`](buffer.md) — `IBuffer`, allocator, external imports, sync.
- [`plugin-sdk.md`](plugin-sdk.md) — `cpipe_node.h`, manifest, lifecycle.
- [`research/00-summary.md`](research/00-summary.md) — master research synthesis; § 7 risk register; § 8 original 12-month roadmap (superseded by this file).
- [`research/_toc.md`](research/_toc.md) — D1–D19 locked decisions and scope.
