# Phase 3 — Editor + Quality Harness

> Date: 2026-05-17 · Phase tag: `v0.4` · Parent: [`roadmap.md`](roadmap.md) · References: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`phase-02-classic-nodes-hdr.md`](phase-02-classic-nodes-hdr.md)

This document is the detailed plan for Phase 3 of the cpipe v1.0 roadmap. P3's job per [`roadmap.md §6`](roadmap.md#6-phase-3--editor--quality-harness-tag-v04) is to take the `v0.3` 18-node classic ISP + HDR (PQ) HEIF surface and grow it into the *editable, quality-tracked* surface that ships at `v1.0`. Concretely:

- A **Web Editor** on GitHub Pages (React Flow 12 / Zustand / Ajv) that renders the 18-node DAG, lets the user mutate per-node parameters live, and round-trips the same `pipeline.cpipe.json` schema offline (RD-10) without a runtime.
- An in-process **editor server** (`cpipe-server`, uWebSockets) that speaks the 8 REST endpoints from [`architecture.md §8`](architecture.md#8-editor-server-surface) plus a binary WebSocket event plane (thumbnail / profile / log / ack / control frames per [`architecture.md §8`](architecture.md#8-editor-server-surface)).
- An **IQA harness**: C++ in-binary PSNR / SSIM / MS-SSIM / ΔE2000 + a Python sidecar (`cpipe_iqa`) wrapping piq + pyiqa for license-isolated metrics ([Research 09 §3.13](research/09-image-quality-benchmarks.md)).
- A **50-image v1 corpus** (manifest-only commit + `scripts/prepare_corpus.py`) drawn from FiveK / SIDD / DND / RAISE / Kalantari HDR + synthetic QBC stand-ins + cpipe-original images.
- A **microbench harness** (`bench/`, Google Benchmark + nanobench) recording per-node + planner + encoder latency to `bench/results/<commit>.json`.
- A **dashboard** (`apps/dashboard/`, Vega-Lite) showing 90-day trend charts for IQA + perf, deployed alongside the editor on `gh-pages`.

Phase 3.A addresses two carried slips before the editor-server work starts:

- **OCIO Vulkan execution** ([P2-PD-74](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)). T1 adds `glslang` via vcpkg, implements `OcioVulkanProcessor::compute_pass` on top of the P2 `VulkanCommandBuffer`, and routes `color.scene_linear_to_display` through the Vulkan compute path on the dev RTX machine.
- **RawTherapee 5.10 tooling for future reference goldens** ([P1-PD-69 / P1-PD-70](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) + the algorithm-matched subset of [P2-PD-61 .. P2-PD-67](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)). T2 lands `tools/golden/rt_render.sh` + an `$RAWTHERAPEE_APPIMAGE` detection path, but P3-PD-55 carries replacement of the seven tiny node-stage EXR goldens until node-matched DNG/pp3 fixtures exist.

What **stays carried** to v1.1: real Pixel / alt-phone QBC DNG corpus ([P2-PD-37 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)); direct Halide Vulkan AOT command-buffer handoff ([P2-PD-59](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)); binary Tracy capture in CI ([P2-PD-42 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) — local-only evidence). P3-PD-55 separately carries RT-derived replacement of the seven tiny node-stage goldens until node-matched DNG/pp3 fixtures exist. P3 explicitly does **not** ship Noise XK pairing, TLS / WSS / LNA / WebRTC / TURN, HDR-aware browser preview, subgraph collapse, AI nodes (P4), or any Android / macOS surface (v1.1 / v1.2).

When P3 is done, the project is tagged `v0.4` and Phase 4 begins.

---

## 1. Objective

A working end-to-end loop that satisfies all four DoD points from [`roadmap.md §6`](roadmap.md#6-phase-3--editor--quality-harness-tag-v04):

1. The Web Editor renders the example 18-node pipeline DAG (`examples/pipelines/full-classic-pipeline.cpipe.json`); the user changes `tone.filmic_rgb.ev`; the runtime re-renders and the editor displays the resulting HEIF thumbnail.
2. The pure-local-JSON mode opens and saves `pipeline.cpipe.json` without a runtime (RD-10), using the File System Access API where available and an `<input type="file">` + Blob URL fallback otherwise.
3. The IQA harness completes a corpus run for the example pipeline and posts results to the dashboard (`bench/results/<commit>.json` → `apps/dashboard/`).
4. The microbench suite runs and records baseline numbers for the 18 classic nodes + scheduler / planner / encode on the development NVIDIA RTX machine.

**Success looks like:**

- `cpipe serve --pipeline examples/pipelines/full-classic-pipeline.cpipe.json --port 4747` boots; `curl http://localhost:4747/api/health` returns 200 OK; the GitHub-Pages-hosted editor at `https://<user>.github.io/cpipe/` pairs over plaintext HTTP / WebSocket and shows the 18-node DAG.
- Mutating `tone.filmic_rgb.ev` in the editor produces a WebP thumbnail update within ~250 ms (200 ms server-side debounce + WS round-trip + decode).
- `cpipe iqa examples/pipelines/full-classic-pipeline.cpipe.json tests/corpus/` produces `bench/results/baseline.json` covering the 50-image v0.4 corpus.
- `cpipe-microbench --benchmark_out=bench/results/<commit>.json` records per-node + planner + encoder timings.
- The dashboard at `https://<user>.github.io/cpipe/dashboard/` renders the PSNR / SSIM / LPIPS / ΔE2000 / wall_ms / peak_rss trends for the last 90 days.
- `ctest --preset linux-debug --output-on-failure` and `ctest --preset linux-release-clang --output-on-failure` are green, including the new `tests/iqa/` and `tests/e2e/` (Playwright) layers.
- `git tag v0.4` is pushed; GitHub Release published.

P3 explicitly does **not** deliver: Noise XK pairing or any non-plaintext editor connectivity (v1.1); HDR-aware browser preview (v1.1); subgraph collapse UI (v1.2); AI nodes (P4); cpipe model verb (P4); Android / macOS / iOS targets (v1.1 / v1.2 / v2); real QBC corpus (v1.1); direct Halide Vulkan AOT command-buffer handoff (v1.1); ACES 2.0 Output Transform fidelity (P5+). See §15 for the full OOS list.

---

## 2. Inputs

- P2 outputs (locked in `v0.3`): the 18-node classic ISP DAG; pipeline-v0.3.json + node-v0.2.json schemas; the SDR / HDR full-classic example pipelines; OCIO v0.2 Looks at `share/cpipe/ocio/v0.2/`; the `cpipe_compute_suite_v1` tail extensions (`submit_halide_with_params`, `submit_ocio_processor`); the `cpipe_host_v1::get_ocio_processor` accessor; the interference-graph memory planner; the precision auto-insert planner + `com.cpipe.precision_convert` built-in; the cpipe-owned `VulkanCommandBuffer` primitive (with the direct Halide AOT command-buffer handoff still carried as [P2-PD-59](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)); the `CPIPE_REGISTER_HALIDE_*` macros + `HalideFilterRegistry`.
- Locked design documents: [`architecture.md`](architecture.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`tech.md`](tech.md), [`roadmap.md`](roadmap.md).
- Locked research: [`research/09-image-quality-benchmarks.md`](research/09-image-quality-benchmarks.md) (IQA + perf harness architecture); [`research/11-pipeline-editor-and-connectivity.md`](research/11-pipeline-editor-and-connectivity.md) (Editor framework + connectivity tiers + per-node UI); [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) (scheduler tap points for thumbnail subscriptions).
- Development machine: Ubuntu 24.04+ with NVIDIA RTX-class GPU, Vulkan 1.3 driver, validation layers, **RawTherapee 5.10 AppImage installed at `$RAWTHERAPEE_APPIMAGE`** (new in P3) for golden regeneration.
- Git LFS installed locally and on the GitHub Actions runner (P1 carry).

---

## 3. Outputs

- Tag `v0.4` on a green `main` commit that satisfies §11 (DoD).
- `apps/web/` — new npm workspace (Vite + React 19 + TypeScript + `@xyflow/react` + Zustand + Ajv); editor SPA deployed to `gh-pages/editor/`.
- `apps/dashboard/` — new npm workspace (Vite + Vega-Lite); IQA + perf dashboard at `gh-pages/dashboard/`.
- `bench/` — new CMake subdir with the `cpipe-microbench` Google Benchmark binary (plus nanobench cases).
- `tests/iqa/`, `tests/e2e/`, `tools/iqa/`, `tools/golden/` — four new top-level directories per the test pyramid extensions.
- `src/cpipe/server/` — new `cpipe-server` static lib (uWebSockets + libwebp) carrying the HTTP + WS surface.
- Schemas: `schemas/pipeline-v0.4.json` (optional `ui` object), `schemas/editor-protocol-v0.1.json` (WS framing + REST envelope JSON-Schema). `schemas/node-v0.2.json` continues from P2 — 8 nodes add live params inside their manifests per [P3-PD-37](#4-phase-decisions-p3-pd-n) without bumping the schema.
- New CLI verbs: `cpipe serve`, `cpipe info`, `cpipe iqa`, `cpipe bench` ([architecture.md §10](architecture.md#10-cli-surface)).
- `tests/corpus/manifest.json` + `tests/corpus/LICENSE.md` + `scripts/prepare_corpus.py` (manifest-only commit; CI fetch + cache job).
- `bench/results/baseline.json` — v0.4 IQA + perf baseline.
- New CI workflows: `pages.yml` (editor + dashboard deploy), `microbench-record.yml` (`workflow_dispatch` only).
- `vcpkg.json` adds `uwebsockets`, `glslang`, `libwebp`, `google-benchmark`, `nanobench`. No new FetchContent.

---

## 4. Phase Decisions (P3-PD-N)

P3-specific decisions, locked from the planning round on 2026-05-17. PD numbering restarts at `P3-PD-1` phase-local. Cross-references `RD-N / D-N / B-N / P-N` stay global; cross-phase references write `P2-PD-NN` / `P1-PD-NN` / `P0-PD-NN`.

| ID | Decision | Value |
|----|----------|-------|
| P3-PD-1  | Phase doc structure | `agent-skills:planning-and-task-breakdown` template (Overview / Architecture Decisions / Task List with checkpoints / Risks / Open Questions) overlaid with the cpipe `P3-PD-N` decision table inside §4 Architecture Decisions. Language: English ([RD-28](roadmap.md#1-decision-quick-reference)). |
| P3-PD-2  | Phase tag | `v0.4` ([RD-16](roadmap.md#1-decision-quick-reference)). |
| P3-PD-3  | Branch / PR policy | One PR per T-task, strictly sequential on `main` (carries [P2-PD-3](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)). Cross-T parallelism not allowed. Sub-task PRs not allowed except where a T explicitly groups two tasks. |
| P3-PD-4  | 24-h release bake | Continued waiver (carries [P2-PD-4 / P1-PD-4 / P0-PD-37](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)): `v0.4` ships when latest `main` CI is green on the release-candidate commit, plus pushed tag and GitHub Release. |
| P3-PD-5  | Sub-phase shape | 7 sub-phases / 24 T-tasks (T0–T23). See §7. |
| P3-PD-6  | Server auth posture | No token. Default bind `127.0.0.1`. `--bind <ip>` (non-loopback) prints a yellow stderr warning before listening. No Noise XK pairing (carries to v1.1 per [RD-8](roadmap.md#1-decision-quick-reference)). |
| P3-PD-7  | Live preview / thumbnail subscription | `node.subscribe_thumbnail {node_id, port, max_size, fps}` over WS; runtime pushes WebP frame after each run to subscribers. Defaults: 256×256, q70, 5 fps. Only the currently selected / on-screen visible nodes subscribed (research [11 §3.4](research/11-pipeline-editor-and-connectivity.md)). |
| P3-PD-8  | REST endpoint set | Eight endpoints per [`architecture.md §8`](architecture.md#8-editor-server-surface): GET `/api/health`, GET `/api/schemas/{node,pipeline}`, GET `/api/registry/nodes`, GET / PUT `/api/pipelines/active`, POST `/api/pipelines/active/params`, POST `/api/pipelines/active/run`, GET `/api/pipelines/active/runs/:run_id`. Single active pipeline per server, last-write-wins. `/api/pair/begin` and `/api/pair/finish` defer to v1.1. |
| P3-PD-9  | WS frame format | Per [`architecture.md §8`](architecture.md#8-editor-server-surface): 13-byte header = 1B type + 4B node-id (big-endian) + 4B timestamp (ms since pipeline start, big-endian) + 4B length (big-endian) + N-byte payload. Frame types `0x01` thumbnail (WebP), `0x02` profile (JSON), `0x03` log (JSON), `0x04` ack (JSON), `0x10` control (JSON). Schema in `schemas/editor-protocol-v0.1.json`. |
| P3-PD-10 | Re-render trigger | Editor sends `node.update_param {node_id, key, value}` control frame; runtime applies the delta and triggers an internal re-run after a 200 ms server-side debounce. No explicit "Render" button for param tweaks. Reloading the DNG / pipeline still requires explicit POST `/api/pipelines/active/run`. |
| P3-PD-11 | Web stack | Vite 6 + React 19 + TypeScript 5.5 + `@xyflow/react` 12 + Zustand 5 + Ajv 8 + Vitest (unit) + Playwright (E2E). No router (single SPA). CSS modules + minimal utility classes. No design system. |
| P3-PD-12 | Offline JSON mode (closes [RD-10](roadmap.md#1-decision-quick-reference)) | Detect `window.showOpenFilePicker` / `showSaveFilePicker`; use FSA where available; otherwise fall back to `<input type="file">` for open and Blob URL download for save. `localStorage` caches last 10 graphs in all cases. Closes [Architecture §17 Q8](architecture.md#17-open-questions) ("desktop-only mode") — see [P3-PD-47](#4-phase-decisions-p3-pd-n). |
| P3-PD-13 | Schema sync | Editor `fetch /api/schemas/{node,pipeline}` on first connect and on every full-page load; cache to `localStorage` with a 7-day TTL keyed on `(host, port)`; **no schema bundled at editor build time**. Initial editor use requires one runtime connection. Banner warns when offline + cache stale. |
| P3-PD-14 | Web build & distribution | `apps/web/` is an independent npm workspace. Two delivery paths: (a) deployed to `gh-pages/editor/` via the new `pages.yml` workflow; (b) installed into `${CMAKE_INSTALL_PREFIX}/share/cpipe/editor/` via a CMake `install` rule that depends on a stamp file `apps/web/dist/.stamp`. `cpipe serve --editor-static <dir>` mounts the static tree at `/editor`. Default: `$CPIPE_EDITOR_STATIC` env, else the installed path. |
| P3-PD-15 | IQA architecture | C++ in-binary metrics: PSNR, SSIM, MS-SSIM, ΔE2000 (Apache-2 reimplementations; OIIO `ImageBufAlgo::compare` continues to back the golden harness). Python sidecar (subprocess) `cpipe_iqa`: piq (LPIPS / DISTS / HaarPSI / BRISQUE / MS-GMSD / VIFp / FSIM / VSI) + pyiqa (MANIQA / TOPIQ / MUSIQ / CLIP-IQA — CC-BY-NC-SA, license-isolated). [Research 09 §3.13](research/09-image-quality-benchmarks.md): "subprocess wins decisively." |
| P3-PD-16 | IQA metric scope at v0.4 | Default per-PR: 4 C++ metrics + a 5-image canary against piq via subprocess. Optional piq full subset via `cpipe iqa --metrics ...`. pyiqa runs in nightly CI only. ~15 metrics total. Aligns with research 09 §2.2 mainline. RD-13 governs: per-node golden gates at PSNR ≥ 40 dB; 50-image corpus is recorded-not-gated until v1.1. |
| P3-PD-17 | Python sidecar packaging | `tools/iqa/cpipe_iqa/` in-tree workspace with `pyproject.toml`; CI `pip install ./tools/iqa[ci]` into a venv. `cpipe-cli` invokes `python -m cpipe_iqa` via `$CPIPE_IQA_PYTHON` env or `python3` on PATH. No PyPI publish. |
| P3-PD-18 | `cpipe iqa` CLI | `cpipe iqa <pipeline.json> <corpus-dir> [--report json\|md\|html] [--metrics psnr,ssim,...] [--baseline <prev.json>]`. Default report is JSON; `--report md` for human review. Per architecture §10. |
| P3-PD-19 | Corpus distribution | Repo commits `tests/corpus/manifest.json` (SHA-256 + upstream URL + license tag + crop spec); `scripts/prepare_corpus.py fetch / verify / list` pulls. CI has one `corpus-fetch` cache step keyed on the manifest SHA-256. No upstream images committed to git ([D11](research/_toc.md#1-decisions-locked-before-research) + [RD-23](roadmap.md#1-decision-quick-reference) license posture). |
| P3-PD-20 | v0.4 corpus composition | 50 single-frame images: 15 daylight FiveK + 5 SIDD + 5 DND + 5 RAISE + 5 Kalantari HDR + 5 synthetic QBC stand-ins (continuing [P2-PD-37 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)) + 10 cpipe-original synthetic scenes. HDR+ Burst slips to P4 with `BatchedBuffer`. |
| P3-PD-21 | Corpus baseline | Baseline-only at v0.4: first-PR-after-T16 IQA numbers freeze as `bench/results/baseline.json`. Subsequent PRs diff against baseline in the dashboard. `baseline.json` finally locked at `v1.0-rc1` per [RD-13](roadmap.md#1-decision-quick-reference) (recorded-not-gated). |
| P3-PD-22 | Corpus license posture | `tests/corpus/LICENSE.md` enumerates each dataset row: upstream license, cpipe usage (fetch-only / LFS / synthetic), restrictions. The CI fetch step prints a per-dataset confirmation line for the P5 license audit ([RD-23](roadmap.md#1-decision-quick-reference)). |
| P3-PD-23 | Microbench library | Google Benchmark + nanobench (both via vcpkg). No Catch2 benchmark. |
| P3-PD-24 | Microbench coverage | All 18 classic nodes + `scheduler::dispatch_node` + `MemoryPlanner::plan_graph_coloring` + `PrecisionPlanner::auto_insert` + DNG OpcodeList2 / OpcodeList3 parsing + HEIF SDR / HDR (PQ) encoding. ~25 microbench cases in `cpipe-microbench`. |
| P3-PD-25 | Microbench runner | CPU-only smoke in `build-and-test.yml`; GPU / Vulkan microbench runs manually on the dev RTX machine via `microbench-record.yml` (`workflow_dispatch`), which uploads `bench/results/<commit>.json` directly. Carries [P2-PD-42](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n): GH-hosted runners have no Vulkan ICD. Recorded-not-gated per [RD-15](roadmap.md#1-decision-quick-reference). |
| P3-PD-26 | `cpipe bench` CLI | Two binaries: `cpipe bench --pipeline <pipe.json> --corpus <dir> [--out report.json]` (end-to-end pipeline timing, inside `cpipe-cli`) + `cpipe-microbench` (standalone Google Benchmark binary in `bench/`). Per architecture §10. |
| P3-PD-27 | Dashboard implementation | `apps/dashboard/` independent npm workspace (Vite + light React + `vega-embed`). Reads `bench/results/*.json` at build time, emits a static SPA. |
| P3-PD-28 | Dashboard data storage | `bench/results/<commit>.json` committed to `main` (per-commit JSONs are a few KB; long-tail acceptable). `bench/results/index.json` maintained by CI on each `microbench-record.yml` run. |
| P3-PD-29 | Dashboard deploy | Single `pages.yml` workflow on `main` push: builds `apps/web/dist/` + `apps/dashboard/dist/` and deploys to the `gh-pages` branch. Editor at `/`, dashboard at `/dashboard/`. Trigger: any change under `apps/web/**`, `apps/dashboard/**`, or `bench/results/**`. |
| P3-PD-30 | Dashboard window | 90-day trend chart by default with per-metric panels: PSNR / SSIM / LPIPS / ΔE2000 / wall_ms / peak_rss. Per-commit drill-down link to GitHub commit. PR delta link (`?compare=<base>...<head>`) is P5 polish. |
| P3-PD-31 | New CLI verbs in P3 | `cpipe serve`, `cpipe info`, `cpipe iqa`, `cpipe bench`. `cpipe model fetch / list / verify` defers to P4 (needs ExecuTorch + `scripts/fetch_models.py`). |
| P3-PD-32 | `cpipe info` subverbs | `cpipe info nodes` (enumerate registered manifests via `host->iterate_registry()`) + `cpipe info gpu` (Vulkan device probe: ICD path, device name, queue families, FP16 / FP32 storage support). `pipelines` and `npu` defer to P4. |
| P3-PD-33 | `cpipe serve` flags | `cpipe serve [--port 4747] [--bind 127.0.0.1] [--pipeline <file>] [--editor-static <dir>]`. settings.json at `~/.config/cpipe/settings.json` + `$CPIPE_*` env overrides per [architecture §9](architecture.md#9-configuration). |
| P3-PD-34 | Pipeline schema bump | `schemas/pipeline-v0.4.json` adds optional `ui` object on each node entry (`x: number?`, `y: number?`, `color: string? (hex)`, `collapsed: boolean?`). Runtime ignores `ui`. Editor falls back to auto-layout when absent. v0.3 fixtures migrate in T0. `Pipeline::load` rejects unmigrated v0.3 with a "schema version mismatch — run `tools/migrate/v03_to_v04.py`" message. |
| P3-PD-35 | Editor protocol schema | `schemas/editor-protocol-v0.1.json` describes the WS framing (per P3-PD-9) + the REST envelope JSON-Schema (`{ ok: bool, data?: T, error?: {code, message} }`). New, no prior version. |
| P3-PD-36 | Node manifest schema | `schemas/node-v0.2.json` unchanged from P2 (params block already supported by P2-PD-12). 8 P3-PD-37 nodes author new param-schema entries inside their existing manifests; manifest schema not bumped. |
| P3-PD-37 | Live param exposure scope | 8 nodes author new manifest params: `tone.filmic_rgb` (ev / contrast / saturation / highlights / shadows), `tone.reinhard` (white_point), `tone.aces_filmic` (toggle), `denoise.bm3d` (+ sigma alongside the P2 `sigma_override`), `denoise.guided_filter` (radius / eps), `denoise.wavelet_bayes_shrink` (chroma_strength), `sharpen.edge_aware_usm` (strength / radius / threshold), `color.3d_lut` (+ interpolation enum: tetrahedral default, trilinear available), `tone.mertens_local` (weight_contrast / saturation / well_exposedness). `linearize`, `blacklevel`, `lens.shading_gainmap`, `lens.dng_opcode_list_3`, `wb.dual_illuminant`, `wb.greyworld_auto`, `colormatrix.dng_to_working`, `demosaic.{rcd,amaze,bilinear,quad_bayer_remosaic}`, `fusion.hdr_plus_derivative`, `color.scene_linear_to_display`, `output.heif_*` stay metadata-driven with empty / structural params. |
| P3-PD-38 | Test pyramid extensions | New layers: `tests/iqa/` (C++ integration tests that spawn `cpipe iqa` as a subprocess) + `tests/e2e/` (Playwright headless against `cpipe serve` + the static editor) + new `tests/unit/test_editor_server.cpp` + `tests/unit/test_editor_protocol.cpp`. Frontend: `apps/web/test/` (Vitest unit) + `apps/dashboard/test/` (Vitest unit). Architecture §13's 4-layer pyramid is preserved; the two frontend layers extend it. |
| P3-PD-39 | CI workflow additions | `build-and-test.yml` extends with: (a) an `iqa` job (4 C++ metrics + CPU `cpipe-microbench` smoke + Playwright E2E); (b) a `corpus-fetch` cache step keyed on `tests/corpus/manifest.json` SHA-256. New `pages.yml` (editor + dashboard deploy on `main` push). New `microbench-record.yml` (`workflow_dispatch` only). |
| P3-PD-40 | OOS scope | Strict explicit list, see §15. |
| P3-PD-41 | Risk register | Seven preset `P3-R1`–`P3-R7` in §10. |
| P3-PD-42 | DoD verification commands | ~14 commands modeled on `phase-02-classic-nodes-hdr.md §11`. See §11. |
| P3-PD-43 | E2E smoke gate | Playwright headless inside `build-and-test.yml`: spawn `cpipe serve --pipeline examples/pipelines/full-classic-pipeline.cpipe.json` as a subprocess, open the editor against `http://localhost:4747`, mutate `tone.filmic_rgb.ev`, observe a thumbnail update via WS, save the pipeline through the FSA fallback (download), reload from the saved JSON, deep-equal the in-memory graph. The E2E job blocks PR merge on failure. |
| P3-PD-44 | OCIO Vulkan retire (closes [P2-PD-74](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)) | T1 adds `glslang` via vcpkg, implements `cpipe::color::OcioVulkanProcessor::compute_pass` on the P2 `VulkanCommandBuffer`, and wires `com.cpipe.color.scene_linear_to_display` to the Vulkan path on the dev RTX machine. CPU fallback retained for no-Vulkan hosts. |
| P3-PD-45 | RT 5.10 retire (closes [P1-PD-69 / P1-PD-70](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) + partial [P2-PD-61 / P2-PD-62 / P2-PD-66 / P2-PD-67](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)) | T2 installs RT 5.10 AppImage on the dev host (`$RAWTHERAPEE_APPIMAGE`), commits `tools/golden/rt_render.sh`, and regenerates 7 RT-referenced goldens (LFS commit + `reference.md` updates): `demosaic.bilinear`, `demosaic.rcd`, `demosaic.amaze`, `wb.dual_illuminant`, `wb.greyworld_auto`, `colormatrix.dng_to_working`, `sharpen.edge_aware_usm`. The other 11 nodes keep their cpipe self-references (their `reference.md` records unchanged). |
| P3-PD-46 | Carried slips at P3 close | Real Pixel / alt-phone QBC DNG corpus (carry [P2-PD-37 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) — target v1.1); direct Halide Vulkan AOT command-buffer handoff (carry [P2-PD-59](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) — target v1.1); binary Tracy capture in CI (carry [P2-PD-42 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) — local-only evidence). |
| P3-PD-47 | Architecture §17 Q8 closure | The editor ships offline-first by default (FSA + fallback + localStorage; runtime connection optional); [Architecture §17 Q8](architecture.md#17-open-questions) is logged as **Resolved in P3-PD-12 + P3-PD-47**. |
| P3-PD-48 | vcpkg additions | `uwebsockets`, `glslang`, `libwebp`, `google-benchmark`, `nanobench`. No new FetchContent. Halide / OCIO / libheif / kvazaar / VMA / Vulkan headers / loader / Tracy / OIIO version pins unchanged. |
| P3-PD-49 | Python deps in `tools/iqa/pyproject.toml` | `piq >= 0.8`, `pyiqa >= 0.1.10`, `numpy`, `OpenImageIO-python` (sidecar reads EXR / HEIF via OIIO's Python binding). All pinned per `v0.4` release; CI installs with `pip install ./tools/iqa[ci]`. |
| P3-PD-50 | What Shipped / What Slipped flow | Same as [P2-PD-46](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n): phase-03 §12 + [`roadmap.md §6`](roadmap.md#6-phase-3--editor--quality-harness-tag-v04) + [`README.md`](../README.md) "Current Status" updated in the closing PR that pushes `v0.4`. |
| P3-PD-51 | OCIO Vulkan language enum | The installed OCIO 2.5.1 headers expose `OCIO::GPU_LANGUAGE_GLSL_VK_4_6`, not the planning name `OCIO::GPU_LANGUAGE_VK_GLSL_4_50`. T1 uses the available OCIO 2.5 enum; the generated shader remains Vulkan GLSL and is compiled to SPIR-V by glslang. |
| P3-PD-52 | glslang sanitizer posture | The vcpkg glslang static library is built without the RTTI symbols needed by UBSAN's `vptr` instrumentation. T1 disables only `-fsanitize=vptr` for `OcioVulkanProcessor.cpp`; ASAN and the rest of UBSAN remain enabled. |
| P3-PD-53 | OCIO LUT upload scope | The v0.2 `scene_linear_rec2020 -> output_{srgb,pq_rec2020}` processors used by `color.scene_linear_to_display` emit no OCIO textures or dynamic uniforms. T1 therefore implements and gates the texture-free Vulkan path needed to retire [P2-PD-74](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n); generic OCIO LUT/uniform upload remains deferred until a runtime path needs it. |
| P3-PD-54 | T1 Vulkan correctness device | The current workspace exposes `Intel(R) UHD Graphics 770 (ADL-S GT1)` as the available Vulkan 1.3 device, not an RTX GPU. T1 correctness evidence uses that device; RTX remains the intended performance/profiling host for later P3 microbench evidence. |
| P3-PD-55 | RT 5.10 node golden carry | T2 adds the RT 5.10 render wrapper and profile placeholders, but does not replace the seven node goldens with RT-derived EXRs. This supersedes the checked-in-golden replacement portion of P3-PD-45. Root cause: the checked-in node fixtures are tiny synthetic stage EXRs (4x4 / 8x8 / 16x16), while RawTherapee renders whole DNG files and does not expose those isolated intermediate node stages. Until node-matched DNG/pp3 fixtures exist, `demosaic.{bilinear,rcd,amaze}`, `wb.{dual_illuminant,greyworld_auto}`, `colormatrix.dng_to_working`, and `sharpen.edge_aware_usm` remain deterministic cpipe self-references. |
| P3-PD-56 | RT TIFF-to-EXR conversion | RawTherapee 5.10 CLI writes TIFF/PNG/JPEG, not EXR. `tools/golden/rt_render.sh` renders a 32-bit TIFF and converts it to EXR with `oiiotool` when available; on this dev host it falls back to ImageMagick `convert` with EXR write support. This is dev-only tooling; CI still does not install RT or the converter. |
| P3-PD-57 | REST envelope exceptions | T4 preserves T3's health shape (`GET /api/health` returns top-level `{ok, abi}`) and serves `/api/schemas/{node,pipeline}` as raw JSON Schema documents for direct Ajv consumption. The registry, active-pipeline, params, run, and run-status control routes use the P3-PD-35 `{ok,data}` / `{ok,error}` REST envelope. |
| P3-PD-58 | T5 WS manual verification | The packaged `/usr/bin/wscat` needs `NODE_PATH=/usr/share/nodejs` and cannot send binary payloads; `npx --yes wscat` is used only for the handshake smoke. The hand-crafted binary subscribe/ack verification is covered by `test_editor_server_ws` and a raw Node/ws client that sends the P3-PD-9 13-byte frame directly. |

---

## 5. Architecture Decisions (cross-cutting)

The PD table locks specific values; this short narrative explains the *why* for the cross-cutting choices.

- **Server-first slicing.** The editor's REST + WS contract is a stable target the Web editor builds against. Building the server first (Phase 3.B) before the editor SPA (Phase 3.C) means the editor never grows mock-server scaffolding that has to be torn out later. The single-maintainer + AI-pair team model ([RD-3](roadmap.md#1-decision-quick-reference)) makes sequential more reliable than parallel anyway.

- **OCIO Vulkan retires before the editor server.** `color.scene_linear_to_display` is the hottest node in any editor-driven re-render loop. Running it on the CPU OCIO processor (the P2 carry) would peg the editor's preview latency budget. Landing the Vulkan compute path in T1 keeps the entire preview loop GPU-resident on the dev RTX machine.

- **RT 5.10 tooling lands before fixture replacement.** The original P3-PD-45 target was to regenerate 7 algorithm-matched node goldens with RawTherapee. P3-PD-55 narrows T2 after implementation evidence: the current goldens are tiny stage EXRs, not DNG renders, so T2 lands the RT wrapper and records the carry in each `reference.md` while keeping the deterministic self-references.

- **Schema sync is fetch-only.** Bundling `schemas/node-v0.2.json` + `schemas/pipeline-v0.4.json` into the editor at build time would force a coordinated release every time the runtime ships a new node manifest field. Fetch-on-connect + 7-day `localStorage` TTL means the editor automatically picks up new node manifests when the user opens it against a newer runtime. The trade-off — first-run requires a runtime connection — is acceptable because the runtime is the source of truth, and the offline mode still works after the first connect (cache survives).

- **C++ in-binary metrics + Python sidecar.** piq is a PyTorch library; "piq in-binary" from `roadmap.md §6` resolves to "shipped with cpipe, invoked via subprocess." The actual in-binary path is a 4-metric C++ implementation (PSNR / SSIM / MS-SSIM / ΔE2000) that covers the golden harness + the per-PR `iqa` job. piq and pyiqa run as a Python subprocess `cpipe_iqa` for the heavier / non-commercial metrics ([Research 09 §3.13](research/09-image-quality-benchmarks.md)).

- **Corpus is manifest-only.** Most v1 corpus datasets are research-license-only (FiveK, SIDD, DND, RAISE, Kalantari HDR). cpipe is Apache-2.0; redistributing those bytes would create a license incompatibility. The manifest-only approach commits SHA-256 + upstream URL + license tag per image; `scripts/prepare_corpus.py` fetches with cache; the CI corpus-fetch cache step is keyed on the manifest SHA-256 ([D11](research/_toc.md#1-decisions-locked-before-research) + [RD-23](roadmap.md#1-decision-quick-reference)).

- **pipeline-v0.4 adds optional `ui`, not required.** Programmatically-generated pipelines (test fixtures, CI smokes, the IQA harness invocations) do not need to declare `x` / `y`. The editor auto-layouts when `ui` is absent. Editor saves with `ui` populated. Runtime ignores `ui`. This avoids forcing every test fixture to learn about layout.

- **Single active pipeline per server.** Multi-session adds substantial REST + WS state machinery for a use case (multi-window concurrent editing) that v1 explicitly defers. `cpipe serve` binds one pipeline at a time; `PUT /api/pipelines/active` replaces it; concurrent editor connections race against `mtime` (last write wins). A banner in the editor warns when a second editor connects to the same server.

- **200 ms debounce server-side.** Slider drags fire dozens of `node.update_param` frames per second. The debounce sits inside `EditorServer::handle_param_update` rather than in the editor because the runtime is the authority on what re-runs are in flight; the editor never needs to know whether a particular delta was coalesced.

- **8 nodes get live params for v0.4.** That's enough to satisfy DoD #1 (`tone.filmic_rgb.ev` slider) plus give the editor a meaningful interactive surface for the dashboard demo. The other 11 nodes stay metadata-driven (their manifest still declares them) so the editor shows them in the DAG but renders an empty params panel — a deliberate "not yet" signal rather than a hidden hint.

---

## 6. Repository Layout (P3 end-state, additions only)

```
cpipe/
├── .github/workflows/
│   ├── build-and-test.yml          # P3 extends: iqa job, corpus-fetch cache, e2e job
│   ├── pages.yml                   # P3-PD-29 NEW
│   └── microbench-record.yml       # P3-PD-25 NEW (workflow_dispatch only)
├── apps/
│   ├── web/                        # P3-PD-14 NEW (independent npm workspace)
│   │   ├── package.json
│   │   ├── vite.config.ts
│   │   ├── tsconfig.json
│   │   ├── index.html
│   │   ├── src/
│   │   │   ├── store/{pipeline,persist,device,schema}.ts
│   │   │   ├── components/{FlowCanvas,nodes/{BaseNode,ParameterForm,ThumbnailHandle},panels/{NodeInspector,DevicePane,Library}}.tsx
│   │   │   ├── ipc/{transport,frames,rpc}.ts
│   │   │   └── index.tsx
│   │   └── test/                   # Vitest unit
│   └── dashboard/                  # P3-PD-27 NEW
│       ├── package.json
│       ├── vite.config.ts
│       ├── src/{specs/*.json,components/*.tsx,index.tsx}
│       └── test/
├── bench/                          # P3-PD-23/24 NEW
│   ├── CMakeLists.txt              # cpipe-microbench (Google Benchmark + nanobench)
│   ├── microbench/                 # ~25 .cpp cases
│   └── results/                    # P3-PD-28: <commit>.json per main commit + baseline.json + index.json
├── docs/
│   └── phase-03-editor-iqa.md      # this file
├── examples/
│   └── pipelines/
│       ├── min-pipeline.cpipe.json (migrated to pipeline-v0.4.json)
│       ├── full-classic-pipeline.cpipe.json     (migrated)
│       └── full-classic-pipeline-hdr.cpipe.json (migrated)
├── include/
│   └── cpipe/
│       ├── color/
│       │   └── OcioVulkanProcessor.hpp  # P2 header, P3 implementation completion (P3-PD-44)
│       └── server/                      # P3 NEW
│           ├── EditorServer.hpp
│           ├── EditorProtocol.hpp
│           ├── ThumbnailEncoder.hpp
│           └── PipelineSession.hpp
├── schemas/
│   ├── pipeline-v0.4.json          # P3-PD-34 NEW
│   └── editor-protocol-v0.1.json   # P3-PD-35 NEW
├── scripts/
│   └── prepare_corpus.py           # P3-PD-19 NEW
├── src/
│   └── cpipe/
│       ├── color/
│       │   └── OcioVulkanProcessor.cpp # P3-PD-44 completion
│       └── server/                      # P3 NEW (cpipe-server static lib)
│           ├── CMakeLists.txt
│           ├── EditorServer.cpp
│           ├── EditorProtocol.cpp
│           ├── ThumbnailEncoder.cpp
│           ├── PipelineSession.cpp
│           ├── HttpRoutes.cpp
│           └── WsRoutes.cpp
├── tests/
│   ├── corpus/
│   │   ├── manifest.json           # P3-PD-19 NEW
│   │   ├── LICENSE.md              # P3-PD-22 NEW
│   │   ├── SUBSTITUTIONS.md        # P3-PD-19 risk fallback log
│   │   ├── pixel8pro.dng           # P1 carry
│   │   └── pixel8pro-qbc.dng       # P2 carry (synthetic stand-in continues per P3-PD-46)
│   ├── e2e/                        # P3-PD-38 NEW (Playwright)
│   │   ├── playwright.config.ts
│   │   ├── editor_serve_iqa.spec.ts
│   │   └── offline_round_trip.spec.ts
│   ├── iqa/                        # P3-PD-38 NEW (C++ integration via cpipe iqa subprocess)
│   │   ├── CMakeLists.txt
│   │   └── test_iqa_corpus_run.cpp
│   ├── golden/                     # P3-PD-55 keeps current self-reference EXR pairs
│   │   ├── demosaic.bilinear/{in,out}.exr     (self-reference; RT carry noted)
│   │   ├── demosaic.rcd/{in,out}.exr          (self-reference; RT carry noted)
│   │   ├── demosaic.amaze/{in,out}.exr        (self-reference; RT carry noted)
│   │   ├── wb.dual_illuminant/{in,out}.exr    (self-reference; RT carry noted)
│   │   ├── wb.greyworld_auto/{in,out}.exr     (self-reference; RT carry noted)
│   │   ├── colormatrix.dng_to_working/{in,out}.exr (self-reference; RT carry noted)
│   │   └── sharpen.edge_aware_usm/{in,out}.exr (self-reference; RT carry noted)
│   ├── integration/
│   │   ├── test_full_classic_pipeline_dng_to_heif_sdr.cpp (P2 carry)
│   │   └── test_full_classic_pipeline_dng_to_heif_hdr.cpp (P2 carry)
│   └── unit/
│       ├── test_editor_server.cpp         # P3-PD-38 NEW
│       ├── test_editor_protocol.cpp       # P3-PD-38 NEW
│       ├── test_ocio_vulkan_processor.cpp # P3-PD-44 NEW
│       └── test_pipeline_v0_4_loader.cpp  # P3-PD-34 NEW
├── tools/
│   ├── golden/                     # P3-PD-55 RT wrapper + carry docs
│   │   └── rt_render.sh
│   └── iqa/                        # P3-PD-17 NEW (Python sidecar)
│       ├── pyproject.toml
│       ├── cpipe_iqa/
│       │   ├── __init__.py
│       │   ├── __main__.py
│       │   ├── metrics_piq.py
│       │   ├── metrics_pyiqa.py
│       │   └── io.py
│       └── tests/
└── vcpkg.json                      # P3-PD-48: add uwebsockets, glslang, libwebp, google-benchmark, nanobench
```

---

## 7. Task List

Twenty-four vertical T-tasks (T0 + T1 .. T23). Seven sub-phase checkpoints. Each task lands a complete, testable slice in dependency order. Sub-task PRs are not allowed (P3-PD-3).

### Phase 3.A — Foundation (3 tasks)

#### T0 — CLI scaffolding + pipeline-v0.4 schema + editor-protocol-v0.1 schema

**Description.** Add empty subcommands `cpipe serve / info / iqa / bench` to `cpipe-cli` (each returning `CPIPE_NOT_IMPLEMENTED` until later T-tasks wire them). Author `schemas/pipeline-v0.4.json` (carry v0.3 schema; add optional `ui` object on node entries: `x?: number`, `y?: number`, `color?: string`, `collapsed?: boolean`). Author `schemas/editor-protocol-v0.1.json` (REST envelope + WS frame format per P3-PD-9). Migrate `examples/pipelines/*.cpipe.json` from v0.3 to v0.4. Update `Pipeline::load` to accept v0.4 and reject v0.3 with a clear "schema version mismatch — run `tools/migrate/v03_to_v04.py`" message.

**Acceptance criteria:**

- [x] `cpipe serve|info|iqa|bench --help` print usage; invoking without further args returns `CPIPE_NOT_IMPLEMENTED` (exit code 100 reserved for `not implemented`).
- [x] `schemas/pipeline-v0.4.json` validates the three migrated example pipelines.
- [x] `schemas/editor-protocol-v0.1.json` validates a handcrafted thumbnail + control frame pair.
- [x] `Pipeline::load` accepts v0.4 fixtures; rejects v0.3 unmigrated with the expected error string.
- [x] `tools/migrate/v03_to_v04.py` migrates a single fixture in place (`x = y = null` left blank for non-editor fixtures).

**Verification:**

- [x] `ctest -R test_pipeline_v0_4_loader` green.
- [x] `ctest -R test_editor_protocol_schema` green.
- [x] `ctest --preset linux-debug --output-on-failure` green (no regressions).

**Dependencies:** None.

**Files likely touched:** `apps/cli/main.cpp` (subcommand stubs); `schemas/pipeline-v0.4.json`; `schemas/editor-protocol-v0.1.json`; `src/cpipe/runtime/Pipeline.cpp`; `examples/pipelines/*.cpipe.json`; `tools/migrate/v03_to_v04.py`; `tests/unit/test_pipeline_v0_4_loader.cpp`; `tests/unit/test_editor_protocol_schema.cpp`.

**Estimated scope:** M (4 sub-stubs + 2 schemas + parser change + migration script + 2 tests).

---

#### T1 — OCIO Vulkan execution (retire P2-PD-74)

**Description.** Add `glslang` to `vcpkg.json`. Implement `cpipe::color::OcioVulkanProcessor::compute_pass` on top of the cpipe-owned `VulkanCommandBuffer` (P2-PD-7). The processor: (a) emits OCIO's Vulkan GLSL via `OCIO::GpuShaderDesc::CreateShaderDesc` with `OCIO::GPU_LANGUAGE_VK_GLSL_4_50`; (b) compiles GLSL → SPIR-V at runtime via glslang; (c) creates a `VkShaderModule` + `VkComputePipeline`; (d) caches keyed on `(config_path, src_cs, dst_cs)`. LUTs are pre-uploaded to dedicated `R32_SFLOAT 1D image` and `R16G16B16A16_SFLOAT 3D image` VMA allocations. Wire `com.cpipe.color.scene_linear_to_display` (P2) to the new GPU path when `CPIPE_VULKAN_AVAILABLE == ON`; CPU fallback retained otherwise.

**Acceptance criteria:**

- [x] `glslang` resolves in vcpkg manifest mode; CMake target links cleanly.
- [x] `OcioVulkanProcessor::compute_pass` produces a result within 0.5 LSB of the CPU processor on a synthetic Rec.2020-linear → sRGB transform (cross-check inside `test_ocio_vulkan_processor`).
- [x] `color.scene_linear_to_display` golden EXR (P2) still passes PSNR ≥ 40 dB.
- [x] Tracy `OcioVulkanProcessor::compute_pass` span point present; `docs/evidence/p3-t1-tracy.tracy.md` records the local run (binary capture tooling remains local-only per P3-PD-46).
- [x] [P2-PD-74](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) marked Resolved-by-P3-T1 in the phase-02 doc footer.

**Verification:**

- [x] `ctest -R test_ocio_vulkan_processor` green on the available Vulkan host (skipped in CI per [P2-PD-42](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n), hardware recorded in P3-PD-54).
- [x] `ctest -R test_node_color_scene_linear_to_display` green on the available Vulkan host (hardware recorded in P3-PD-54).
- [x] `ctest -L golden` PSNR ≥ 40 dB for `color.scene_linear_to_display`.

**Dependencies:** T0.

**Files likely touched:** `vcpkg.json`; `include/cpipe/color/OcioVulkanProcessor.hpp`; `src/cpipe/color/OcioVulkanProcessor.cpp`; `src/cpipe/nodes/color/scene_linear_to_display.cpp`; `tests/unit/test_ocio_vulkan_processor.cpp`; `docs/evidence/p3-t1-tracy.tracy.md`; `docs/phase-02-classic-nodes-hdr.md` (P2-PD-74 footer).

**Estimated scope:** L (vcpkg dep + 1 GPU processor implementation + 1 wired node + 1 test + Tracy evidence).

---

#### T2 — RawTherapee 5.10 tooling + 7-node golden carry (P1-PD-69/70 + partial P2-PD-61..67)

**Description.** Install RawTherapee 5.10 AppImage on the dev host at `$RAWTHERAPEE_APPIMAGE`. Commit `tools/golden/rt_render.sh` that wraps RT's CLI mode (`rawtherapee-cli`) — invokes RT with a `.pp3` profile and emits EXR through a TIFF conversion step. The original target was to regenerate seven RT-referenced goldens (`demosaic.bilinear`, `demosaic.rcd`, `demosaic.amaze`, `wb.dual_illuminant`, `wb.greyworld_auto`, `colormatrix.dng_to_working`, `sharpen.edge_aware_usm`); P3-PD-55 records why those replacements remain carried and why the existing deterministic self-reference pairs stay checked in.

**Acceptance criteria:**

- [x] `tools/golden/rt_render.sh <input.dng> <profile.pp3> <output.exr>` succeeds on the dev host.
- [x] All 7 existing self-reference EXR pairs pass PSNR ≥ 40 dB against the cpipe implementation under `ctest -L golden`; RT replacement is carried by P3-PD-55.
- [x] Each `tests/golden/<node>/reference.md` updated: records deterministic self-reference provenance plus the P3-PD-55 RT carry.
- [x] [P1-PD-69 / P1-PD-70](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) footer + the 4 partial P2-PD rows ([P2-PD-61 / P2-PD-62 / P2-PD-66 / P2-PD-67](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)) marked carried with rationale.
- [x] CI does not install RT; no regenerated LFS goldens are checked in per P3-PD-55.

**Verification:**

- [x] `ctest -L golden` PSNR ≥ 40 dB for the 7 nodes.
- [x] `tools/golden/rt_render.sh --self-test` returns 0 on the dev host.

**Dependencies:** T0.

**Files likely touched:** `tools/golden/rt_render.sh`; `tools/golden/profiles/*.pp3`; 7 × `tests/golden/<node>/{in,out}.exr` (LFS); 7 × `tests/golden/<node>/reference.md`; `docs/phase-01-walking-skeleton.md` (PD footer); `docs/phase-02-classic-nodes-hdr.md` (PD footers).

**Estimated scope:** L (1 tooling script + RT profile placeholders + 7 reference.md + phase-doc carry updates).

---

### Checkpoint A — after T0–T2

- [x] All three tasks merged; `main` is green.
- [x] pipeline-v0.4 + editor-protocol-v0.1 schemas land; example pipelines migrated.
- [x] `cpipe serve / info / iqa / bench --help` print usage; bodies return `CPIPE_NOT_IMPLEMENTED`.
- [x] OCIO Vulkan processor runs `color.scene_linear_to_display` end-to-end on the available Vulkan host; [P2-PD-74](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n) Resolved. RTX remains the later profiling host per P3-PD-54.
- [x] RT 5.10 wrapper added and self-tested; 7 existing self-reference goldens still pass PSNR ≥ 40 dB; RT-derived replacement is carried by P3-PD-55.
- [x] No regression on P1 / P2 unit and integration tests.

---

### Phase 3.B — Editor server (5 tasks)

#### T3 — `cpipe-server` target + uWebSockets integration

**Description.** Author the new `cpipe-server` static lib (depends on `cpipe-runtime` + `uwebsockets` + `libwebp`). Add `uwebsockets` and `libwebp` to vcpkg manifest. Land a minimal `EditorServer` skeleton with one route: `GET /api/health` returns `{ ok: true, abi: { major: 1, minor: 3 } }`. `cpipe serve --port 4747 --bind 127.0.0.1` boots the server; `Ctrl-C` shuts it down cleanly.

**Acceptance criteria:**

- [x] `cpipe-server` target builds; links cleanly into `cpipe-cli`.
- [x] `cpipe serve --port 4747` exits 0 on `Ctrl-C`; `curl http://localhost:4747/api/health` returns 200 with valid JSON.
- [x] uWebSockets I/O thread is its own thread; main thread continues to drive `Pipeline::run` per [architecture §4](architecture.md#4-process-and-thread-model).
- [x] Tracy `EditorServer::handle_request` span visible (when `CPIPE_ENABLE_TRACY == ON`).

**Verification:**

- [x] `ctest -R test_editor_server_health` green.
- [x] `cpipe serve --port 4747 &` + `curl localhost:4747/api/health` returns 200 OK.

**Dependencies:** Checkpoint A.

**Files likely touched:** `vcpkg.json`; `include/cpipe/server/EditorServer.hpp`; `src/cpipe/server/{CMakeLists.txt,EditorServer.cpp,HttpRoutes.cpp}`; `apps/cli/serve_command.cpp`; `tests/unit/test_editor_server_health.cpp`.

**Estimated scope:** L (new CMake target + 2 vcpkg deps + server skeleton + 1 test).

---

#### T4 — REST control plane (8 endpoints)

**Description.** Implement the 8 REST endpoints from [architecture §8](architecture.md#8-editor-server-surface): GET `/api/schemas/{node,pipeline}` (serve from embedded resources — bake `schemas/node-v0.2.json` + `schemas/pipeline-v0.4.json` into the binary), GET `/api/registry/nodes` (iterate `host->iterate_registry()`), GET / PUT `/api/pipelines/active` (load / read the current pipeline), POST `/api/pipelines/active/params` (apply a `{node_id, key, value}` delta), POST `/api/pipelines/active/run` (trigger a re-run, return `{run_id}`), GET `/api/pipelines/active/runs/:run_id` (status + output paths). Single active pipeline per server; PUT replaces. The REST envelope follows P3-PD-35: `{ ok: true, data: T }` on success, `{ ok: false, error: {code, message} }` on failure.

**Acceptance criteria:**

- [x] Control endpoints respond with the expected envelope; health and schema routes use the P3-PD-57 raw JSON exceptions.
- [x] PUT `/api/pipelines/active` with a v0.4 graph replaces the active pipeline; subsequent GET returns the same graph.
- [x] POST `/api/pipelines/active/params` with `{node_id: "tone", key: "ev", value: 0.5}` updates the in-memory param map.
- [x] POST `/api/pipelines/active/run` returns a `run_id`; GET `/api/pipelines/active/runs/:run_id` reports completion within ~5 s on the dev host.

**Verification:**

- [x] `ctest -R test_editor_server_rest` green.
- [x] `curl -X POST -d '{...}' localhost:4747/api/pipelines/active/params` returns 200 with the expected envelope.

**Dependencies:** T3.

**Files likely touched:** `include/cpipe/server/EditorServer.hpp`; `src/cpipe/server/{EditorServer.cpp,HttpRoutes.cpp,PipelineSession.cpp}`; `tests/unit/test_editor_server_rest.cpp`.

**Estimated scope:** L (~600 LOC across server routes + state + 1 test with ~12 cases).

---

#### T5 — WebSocket event plane

**Description.** Add the WS endpoint `/ws` to `cpipe-server`. Implement the binary frame format per P3-PD-9 (13-byte header + payload). Client subscriptions live in `PipelineSession`. Control frames (JSON, `0x10`) are bidirectional: the editor can send `{type: "node.subscribe_thumbnail", node_id, port, max_size, fps}` or `{type: "node.update_param", ...}`. Ack frames (`0x04`) confirm receipt. Profile frames (`0x02`) and log frames (`0x03`) are runtime → editor only.

**Acceptance criteria:**

- [x] WS handshake completes against `wscat` on the dev host (via `npx --yes wscat`, per P3-PD-58).
- [x] Binary frame round-trip: editor sends `node.subscribe_thumbnail` control frame; server replies with an ack frame within 50 ms.
- [x] Profile frames flow during a `pipeline.run`; per-node `{ms, mem_kb}` payloads land in raw WS client output.
- [x] Log frames carry INFO-level JSON lines (filtered by level on the editor side).

**Verification:**

- [x] `ctest -R test_editor_server_ws` green.
- [x] `npx --yes wscat -c ws://localhost:4747/ws` handshakes; raw Node/ws client accepts a hand-crafted subscribe frame and prints the ack (P3-PD-58).

**Dependencies:** T4.

**Files likely touched:** `include/cpipe/server/{EditorProtocol.hpp,PipelineSession.hpp}`; `src/cpipe/server/{EditorProtocol.cpp,PipelineSession.cpp,WsRoutes.cpp}`; `tests/unit/test_editor_server_ws.cpp`.

**Estimated scope:** L (frame parser + session state + WS routes + 1 multi-frame test).

---

#### T6 — Thumbnail subscription + WebP encode

**Description.** Implement the thumbnail tap inside `Scheduler::dispatch_node`: after a node's `process()` completes and its output `IBuffer` is frozen, if any client is subscribed to that node's output port, schedule a downsample + WebP encode task on the TaskFlow executor; emit the resulting bytes as a `0x01` thumbnail frame over WS. Downsample to 256×256 (configurable per subscription); encode with libwebp at q70 (configurable). Per-port subscriptions: at most one encode per `fps_interval` across all clients per port. Tracy `EditorServer::push_thumbnail` span instruments the work.

**Acceptance criteria:**

- [ ] After a subscribed `pipeline.run`, editor receives at least one WebP frame within 250 ms (200 ms debounce + WS + encode).
- [ ] Concurrent subscribers (up to 4) all receive the same frame within ±20 ms.
- [ ] Subscribers > 4: encode rate degrades to 2 fps (P3-R4 mitigation); subscribers > 4 logged at INFO.
- [ ] Unsubscribe stops thumbnail traffic for that node within one frame.

**Verification:**

- [ ] `ctest -R test_thumbnail_subscription` green.
- [ ] Local Tracy capture shows `EditorServer::push_thumbnail` spans in the expected cadence.

**Dependencies:** T5.

**Files likely touched:** `include/cpipe/server/ThumbnailEncoder.hpp`; `src/cpipe/server/{ThumbnailEncoder.cpp,PipelineSession.cpp,WsRoutes.cpp}`; `src/cpipe/runtime/Scheduler.cpp` (tap point); `tests/unit/test_thumbnail_subscription.cpp`.

**Estimated scope:** L (1 encoder + scheduler tap + multi-subscriber state machine + 1 test).

---

#### T7 — `cpipe serve` CLI + settings.json + LAN warning

**Description.** Complete the `cpipe serve` CLI body: parse `--port`, `--bind`, `--pipeline`, `--editor-static` flags; layer settings.json + `$CPIPE_*` env per [architecture §9](architecture.md#9-configuration). On non-loopback `--bind`, print a yellow stderr warning to the operator (`WARNING: binding to <ip>:<port>; this server has no authentication and may expose your runtime to anyone on the LAN. RD-8 documents this risk.`). If `--editor-static <dir>` is provided (or `$CPIPE_EDITOR_STATIC` env or the installed default), mount it at `/editor`.

**Acceptance criteria:**

- [ ] `cpipe serve --port 4747 --bind 127.0.0.1` boots; `curl localhost:4747/api/health` returns 200.
- [ ] `cpipe serve --bind 0.0.0.0` boots after printing the yellow LAN warning to stderr.
- [ ] `cpipe serve --editor-static apps/web/dist` mounts the editor at `/editor`; `curl localhost:4747/editor/` returns `index.html`.
- [ ] `~/.config/cpipe/settings.json` overrides apply; CLI flags override settings.json.

**Verification:**

- [ ] `ctest -R test_cpipe_serve_cli` green.
- [ ] Manual: `cpipe serve --bind 0.0.0.0` emits the LAN warning on stderr.

**Dependencies:** T6.

**Files likely touched:** `apps/cli/serve_command.cpp`; `src/cpipe/runtime/Settings.cpp` (extension); `tests/unit/test_cpipe_serve_cli.cpp`.

**Estimated scope:** M (CLI11 wiring + 3 config sources + 1 warning emitter + 1 test).

---

### Checkpoint B — after T3–T7

- [ ] All five tasks merged; `main` is green.
- [ ] `cpipe serve` boots; 8 REST endpoints round-trip; WS frame format works.
- [ ] Thumbnail subscription delivers WebP frames; per-port debounce + per-port subscriber cap honored.
- [ ] `cpipe serve --bind 0.0.0.0` warns operators about the unauthenticated LAN exposure.
- [ ] `cpipe serve --editor-static <dir>` mounts a static SPA path.
- [ ] No regression on P1 / P2 / Phase 3.A tests.

---

### Phase 3.C — Web editor (5 tasks)

#### T8 — `apps/web/` scaffold

**Description.** Author the npm workspace under `apps/web/` per research 11 §3.3. `package.json` pins Vite 6, React 19, TypeScript 5.5, `@xyflow/react` 12, Zustand 5, Ajv 8, Vitest, Playwright. Scaffold the directory tree (`src/store/*.ts`, `src/components/*`, `src/ipc/*`, `src/index.tsx`). Lint via `eslint` + `prettier` (pinned via package.json devDependencies). A minimal `index.tsx` mounts `<App>` that just renders "cpipe editor loading…". CI workflow `pages.yml` builds + uploads `apps/web/dist/` (no deploy yet — that's T22). CMake adds an `install` rule that depends on `apps/web/dist/.stamp` and copies into `${CMAKE_INSTALL_PREFIX}/share/cpipe/editor`.

**Acceptance criteria:**

- [ ] `cd apps/web && npm ci && npm run build` produces `apps/web/dist/index.html`.
- [ ] `cd apps/web && npm run lint && npm run test` green.
- [ ] `cmake --build` invoking `--target install` copies `apps/web/dist/` into `${CMAKE_INSTALL_PREFIX}/share/cpipe/editor` when `apps/web/dist/.stamp` exists.
- [ ] Hot-reload `npm run dev` boots in <2 s.

**Verification:**

- [ ] `cd apps/web && npm run build && ls dist/index.html` returns 0.
- [ ] `ctest -R test_install_editor_static` green (verifies install rule).

**Dependencies:** Checkpoint B.

**Files likely touched:** `apps/web/{package.json,vite.config.ts,tsconfig.json,index.html,eslint.config.mjs,src/index.tsx,src/App.tsx,test/App.test.tsx}`; CMake `install` rule in top-level `CMakeLists.txt`; `tests/unit/test_install_editor_static.cpp`.

**Estimated scope:** L (npm workspace scaffold + CMake install + 1 test).

---

#### T9 — React Flow canvas + custom node component + schema fetch

**Description.** Implement `<FlowCanvas>` wrapping `<ReactFlow>` (research 11 §3.3). Custom `<BaseNode>` renders header (category-color + label + ID-tooltip), thumbnail slot (placeholder for T10), parameter form slot (placeholder), and typed port handles. Zustand `pipelineSlice` holds nodes / edges / selectedId / viewport. `schemaSlice` fetches `/api/schemas/{node,pipeline}` on first connect and caches to localStorage with 7-day TTL keyed on `(host, port)`. Ajv compiles the manifest schema at boot; validators are exposed via the store. The editor renders the example 18-node DAG when given a v0.4 pipeline JSON (initially loaded from a hardcoded `examples/pipelines/full-classic-pipeline.cpipe.json` fetch).

**Acceptance criteria:**

- [ ] Editor renders all 18 nodes from `full-classic-pipeline.cpipe.json` in their declared `ui.x` / `ui.y` positions; missing `ui` falls back to React Flow's auto-layout.
- [ ] Custom node component shows category color, label, ID tooltip on hover.
- [ ] Schema fetch happens on first connect; `localStorage[cpipe.schemas]` populated with a 7-day TTL.
- [ ] Banner shows when the cached schema is stale and the runtime is unreachable.

**Verification:**

- [ ] `cd apps/web && npm run test` green (Vitest unit tests for store + components).
- [ ] Local: `cpipe serve --pipeline examples/pipelines/full-classic-pipeline.cpipe.json &` + open `http://localhost:5173/` shows the 18-node DAG.

**Dependencies:** T8.

**Files likely touched:** `apps/web/src/components/{FlowCanvas.tsx,nodes/{BaseNode.tsx,ThumbnailHandle.tsx}}`; `apps/web/src/store/{pipeline.ts,schema.ts,persist.ts}`; `apps/web/src/ipc/transport.ts`; `apps/web/test/`.

**Estimated scope:** L (~800 LOC across components + store + 6+ Vitest cases).

---

#### T10 — Param-form auto-generation + live param updates (DoD #1)

**Description.** `<ManifestForm>` consumes a node's manifest `params` array and renders one widget per param: number → slider, enum → dropdown, boolean → checkbox, string → text input. Editing emits an Ajv-validated `node.update_param` control frame over WS. Zustand store optimistically updates; the runtime's ack frame confirms (or rolls back on error). `<NodeInspector>` panel binds the selected node's params + a live thumbnail subscription (256×256 q70 5 fps on the selected node's primary output port). When the runtime's `pipeline.applied` post-resolution graph arrives, the editor renders implicit `precision_convert` nodes as half-height "convert" nodes (research 11 §3.4).

**Acceptance criteria:**

- [ ] Moving the `tone.filmic_rgb.ev` slider in the editor produces a WebP thumbnail update within ~250 ms.
- [ ] Ajv validation rejects out-of-range values; UI shows the error inline.
- [ ] Implicit `precision_convert` nodes render in a half-height "convert" shape.
- [ ] Param updates are coalesced server-side after 200 ms debounce; multiple slider drags within 200 ms produce one re-run.

**Verification:**

- [ ] `cd apps/web && npm run test` green (Vitest mock-WS tests).
- [ ] Local end-to-end: edit `tone.filmic_rgb.ev` and see thumbnail update.

**Dependencies:** T9.

**Files likely touched:** `apps/web/src/components/{nodes/ParameterForm.tsx,panels/NodeInspector.tsx,panels/DevicePane.tsx}`; `apps/web/src/ipc/{frames.ts,rpc.ts}`; `apps/web/src/store/{pipeline.ts,device.ts}`; `apps/web/test/`.

**Estimated scope:** L (param-form generator + WS control plane + 1 panel + 10+ Vitest cases).

---

#### T11 — Offline JSON mode (RD-10, closes Architecture §17 Q8)

**Description.** Detect `window.showOpenFilePicker` / `showSaveFilePicker`; when available, use the FSA API to open / save `pipeline.cpipe.json` directly. When unavailable (Safari, Firefox without flag), fall back to `<input type="file">` for open and Blob URL download for save. localStorage caches the last 10 graphs in both code paths. The editor explicitly works without a runtime in offline mode: schema fetch fails silently → fall back to the cached schema (or to a build-time pinned default schema bundled with the editor for first-run); FlowCanvas still renders and edits. RD-10 is the contract.

**Acceptance criteria:**

- [ ] On Chrome 142+, FSA path: opening + editing + saving a `pipeline.cpipe.json` round-trips without a runtime.
- [ ] On Firefox 147 / Safari 19, fallback path: opens via `<input type="file">`, saves via Blob URL download.
- [ ] localStorage maintains last 10 graphs across reloads.
- [ ] Offline mode banner shows "no runtime connected" without breaking edits.

**Verification:**

- [ ] `cd apps/web && npm run test` green (Vitest with FSA mock).
- [ ] Playwright spec `tests/e2e/offline_round_trip.spec.ts` exercises both code paths (T23 lands the spec; here we verify the mechanism).

**Dependencies:** T10.

**Files likely touched:** `apps/web/src/ipc/transport.ts`; `apps/web/src/store/persist.ts`; `apps/web/src/components/panels/Library.tsx`; `apps/web/test/`.

**Estimated scope:** M (FSA + fallback + cache + 5+ Vitest cases).

---

#### T12 — 8-node param schema authoring (P3-PD-37)

**Description.** Author manifest `params` entries for the 8 P3-PD-37 nodes. For each, add unit-test coverage that the param updates flow end-to-end (set in editor → runtime re-runs → output changes). Update each node's `manifest.json` with a `params` array per the node-v0.2 schema; `Pipeline::load` validates ranges on load (per P2-PD-11). Editor automatically renders the new sliders / enums.

The 8 nodes:

- `com.cpipe.tone.filmic_rgb`: `ev` (number −2..2), `contrast` (number 0.5..2), `saturation` (number 0..2), `highlights` (number 0..2), `shadows` (number 0..2).
- `com.cpipe.tone.reinhard`: `white_point` (number 0.1..10).
- `com.cpipe.tone.aces_filmic`: `toggle` (boolean, default true).
- `com.cpipe.denoise.bm3d`: `sigma` (number 0..0.2; defaults to NoiseProfile-derived); `sigma_override` (number, P2 carry).
- `com.cpipe.denoise.guided_filter`: `radius` (integer 1..32), `eps` (number 1e-5..1e-1).
- `com.cpipe.denoise.wavelet_bayes_shrink`: `chroma_strength` (number 0..2).
- `com.cpipe.sharpen.edge_aware_usm`: `strength` (number 0..2), `radius` (integer 1..32), `threshold` (number 0..0.1).
- `com.cpipe.color.3d_lut`: `lut_path` (string, P2 carry), `interpolation` (enum: tetrahedral|trilinear, default tetrahedral).
- `com.cpipe.tone.mertens_local`: `weight_contrast` (number 0..2), `weight_saturation` (number 0..2), `weight_well_exposedness` (number 0..2).

**Acceptance criteria:**

- [ ] All 8 manifests validate against `schemas/node-v0.2.json`.
- [ ] Each node's `process()` honors the new params (range-clamped at runtime).
- [ ] Editor renders the sliders / dropdowns; Ajv validation prevents out-of-range commits.
- [ ] `tests/unit/test_node_<name>_params` green for all 8 nodes.

**Verification:**

- [ ] `ctest -R 'test_node_(tone_filmic_rgb|tone_reinhard|tone_aces_filmic|denoise_bm3d|denoise_guided_filter|denoise_wavelet_bayes_shrink|sharpen_edge_aware_usm|color_3d_lut|tone_mertens_local)_params'` green.
- [ ] `cd apps/web && npm run test` green (param form renders all 8 node variants).

**Dependencies:** T11.

**Files likely touched:** 9 × `src/cpipe/nodes/.../<node>.json` (manifest updates); 9 × `src/cpipe/nodes/.../<node>.cpp` (param-aware `process()`); 9 × `tests/unit/test_node_<name>_params.cpp`; `apps/web/test/ParameterForm.test.tsx`.

**Estimated scope:** L (~1500 LOC across 9 nodes + 9 tests + frontend coverage).

---

### Checkpoint C — after T8–T12

- [ ] All five tasks merged; `main` is green.
- [ ] `apps/web/` builds & installs; editor renders the 18-node DAG; schema fetch + cache works.
- [ ] DoD #1 satisfied locally: moving `tone.filmic_rgb.ev` slider produces a thumbnail update.
- [ ] DoD #2 satisfied locally: offline FSA + fallback round-trip works.
- [ ] All 8 P3-PD-37 nodes have live params; tests + editor render coverage green.
- [ ] No regression on P1 / P2 / Phase 3.A / Phase 3.B.

---

### Phase 3.D — IQA harness (4 tasks)

#### T13 — `tools/iqa/cpipe_iqa/` Python sidecar + C++ 4 metrics + `cpipe iqa` CLI

**Description.** Author the Python sidecar `tools/iqa/cpipe_iqa/` per P3-PD-17: `pyproject.toml` pins piq, pyiqa, numpy, OpenImageIO-python. `python -m cpipe_iqa --image-a a.heif --image-b b.heif --metrics psnr,ssim,lpips_alex --report json` emits per-metric JSON. Implement the C++ in-binary 4 metrics (PSNR, SSIM, MS-SSIM, ΔE2000 — `cpipe::iqa::*` module; OIIO's `ImageBufAlgo::compare` continues to back the golden harness). Implement the `cpipe iqa` CLI body per P3-PD-18: parses `<pipeline.json> <corpus-dir> [--report ...] [--metrics ...] [--baseline ...]`; spawns the Python sidecar for the non-C++ metrics; aggregates results into `bench/results/<commit>.json` (or `--out report.json`).

**Acceptance criteria:**

- [ ] `pip install ./tools/iqa[ci]` succeeds in a fresh Python 3.12 venv.
- [ ] `python -m cpipe_iqa --image-a foo.heif --image-b bar.heif --metrics psnr,ssim,lpips_alex` prints valid JSON with the 3 metric scores.
- [ ] `cpipe::iqa::psnr(a, b)` agrees with piq within 0.05 dB on a 5-image canary; same for SSIM (0.001), MS-SSIM (0.002), ΔE2000 (0.1).
- [ ] `cpipe iqa examples/pipelines/full-classic-pipeline.cpipe.json tests/corpus/` runs end-to-end; emits a JSON report.

**Verification:**

- [ ] `ctest -R test_iqa_cpp_metrics` green.
- [ ] `ctest -R test_iqa_subprocess` green (spawns Python sidecar against a 2-image canary).
- [ ] `cpipe iqa --help` prints the CLI usage.

**Dependencies:** Checkpoint C.

**Files likely touched:** `tools/iqa/{pyproject.toml,cpipe_iqa/{__init__.py,__main__.py,metrics_piq.py,metrics_pyiqa.py,io.py},tests/}`; `include/cpipe/iqa/Metrics.hpp`; `src/cpipe/iqa/Metrics.cpp`; `apps/cli/iqa_command.cpp`; `tests/unit/{test_iqa_cpp_metrics.cpp,test_iqa_subprocess.cpp}`.

**Estimated scope:** L (Python package + 4 C++ metrics + CLI body + 2 test categories).

---

#### T14 — Corpus manifest + `scripts/prepare_corpus.py` + LICENSE.md

**Description.** Author `tests/corpus/manifest.json` with rows for each image: `{id, sha256, url, license, crop, dataset}`. Land `scripts/prepare_corpus.py {fetch, verify, list}` that pulls per the manifest into `tests/corpus/` (or `--subset minimal` for 10 images, ~600 MB — P3-R7 mitigation). Author `tests/corpus/LICENSE.md` (P3-PD-22) enumerating each dataset's upstream license + cpipe usage. CI corpus-fetch step caches keyed on the manifest SHA-256.

**Acceptance criteria:**

- [ ] `python scripts/prepare_corpus.py fetch` succeeds against a fresh dev host; total disk under 5 GB.
- [ ] `python scripts/prepare_corpus.py verify` checks all SHA-256.
- [ ] `python scripts/prepare_corpus.py list --subset minimal` lists 10 entries.
- [ ] `tests/corpus/LICENSE.md` covers every manifest entry.
- [ ] CI caches the corpus by manifest SHA-256 (second CI run skips fetch).

**Verification:**

- [ ] `scripts/prepare_corpus.py verify` exits 0 on the dev host.
- [ ] CI workflow run shows `corpus-fetch` cache hit on the second run.

**Dependencies:** T13.

**Files likely touched:** `tests/corpus/manifest.json`; `tests/corpus/LICENSE.md`; `tests/corpus/SUBSTITUTIONS.md`; `scripts/prepare_corpus.py`; `.github/workflows/build-and-test.yml` (corpus-fetch cache).

**Estimated scope:** M (manifest schema + fetch script + license doc + CI cache wiring).

---

#### T15 — v0.4 50-image corpus authoring

**Description.** Curate the 50 images per P3-PD-20: 15 daylight FiveK + 5 SIDD + 5 DND + 5 RAISE + 5 Kalantari HDR + 5 synthetic QBC stand-ins (continuing [P2-PD-37 / P2-PD-76](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)) + 10 cpipe-original synthetic scenes. Each row of `manifest.json` documents: `id`, `sha256` (after crop), `url` (upstream), `license`, `crop` (`{x, y, w, h}` rectangle or "full"), `dataset`. Crop scripts (Python OIIO) live in `scripts/corpus/`. The cpipe-original synthetic scenes are committed to LFS under `tests/corpus/synthetic/` (Apache-2.0).

**Acceptance criteria:**

- [ ] Manifest has 50 entries, each with a verified SHA-256 after fetch + crop.
- [ ] 5 synthetic QBC stand-ins land in `tests/corpus/synthetic/qbc/` (LFS).
- [ ] 10 cpipe-original synthetic scenes land in `tests/corpus/synthetic/cpipe/` (LFS).
- [ ] Each row's license noted in `LICENSE.md`.

**Verification:**

- [ ] `python scripts/prepare_corpus.py verify` exits 0 with 50/50 verified.

**Dependencies:** T14.

**Files likely touched:** `tests/corpus/manifest.json` (50 rows); `tests/corpus/synthetic/{qbc,cpipe}/*` (LFS); `scripts/corpus/{crop_fivek.py,crop_sidd.py,...}`; `tests/corpus/LICENSE.md`.

**Estimated scope:** L (50 manifest rows + 15 LFS synthetic images + 6 crop scripts).

---

#### T16 — Corpus baseline run + `bench/results/baseline.json`

**Description.** Run `cpipe iqa examples/pipelines/full-classic-pipeline.cpipe.json tests/corpus/ --report json --metrics psnr,ssim,ms_ssim,delta_e2000,lpips_alex,dists,haarpsi,brisque --out bench/results/baseline.json` on the dev host; commit the baseline. The `bench/results/index.json` is generated by CI from the existing per-commit JSONs.

**Acceptance criteria:**

- [ ] `bench/results/baseline.json` covers all 50 corpus images for the 8 default metrics.
- [ ] Each image entry has per-metric numbers; corpus-level summary (mean / median / 5th / 95th).
- [ ] `bench/results/index.json` lists the baseline commit + 1 follow-up commit.

**Verification:**

- [ ] `cat bench/results/baseline.json | jq '.images | length'` returns 50.
- [ ] `ctest -L iqa` runs against `bench/results/baseline.json` for the 5-image CI canary.

**Dependencies:** T15.

**Files likely touched:** `bench/results/baseline.json`; `bench/results/index.json`; `tests/iqa/CMakeLists.txt`; `tests/iqa/test_iqa_corpus_run.cpp`.

**Estimated scope:** M (1 baseline run + index generator + 1 ctest integration).

---

### Checkpoint D — after T13–T16

- [ ] All four tasks merged; `main` is green.
- [ ] `cpipe iqa` works end-to-end; Python sidecar installs cleanly.
- [ ] Corpus fetches in <5 min on a fresh CI runner with the cache miss path.
- [ ] `bench/results/baseline.json` committed; `tests/iqa/` integration test runs the 5-image canary.
- [ ] DoD #3 satisfied: IQA corpus run completes; baseline committed.

---

### Phase 3.E — Microbench harness (3 tasks)

#### T17 — `bench/` scaffold + `cpipe-microbench` binary

**Description.** New CMake subdir `bench/` with the `cpipe-microbench` executable (links `cpipe-runtime` + `cpipe-builtin-nodes` + Google Benchmark + nanobench). Land a 3-case smoke: `BM_PassthroughNode`, `BM_DemosaicBilinear`, `BM_OutputHeifSdrEncode`. Each emits Google Benchmark JSON (`--benchmark_format=json --benchmark_out=bench/results/<commit>.json`). Add CPU-only smoke to `build-and-test.yml`.

**Acceptance criteria:**

- [ ] `cpipe-microbench --benchmark_filter=BM_Passthrough` runs in <2 s.
- [ ] `cpipe-microbench --benchmark_format=json --benchmark_out=/tmp/bench.json` emits valid JSON.
- [ ] CI's `build-and-test.yml` runs the CPU smoke on every PR; output uploaded as artifact.

**Verification:**

- [ ] `ctest -R cpipe-microbench-smoke` green.
- [ ] CI artifact `bench-smoke-<commit>.json` visible in the GH Actions UI.

**Dependencies:** Checkpoint D.

**Files likely touched:** `vcpkg.json` (google-benchmark + nanobench); `bench/CMakeLists.txt`; `bench/microbench/{bm_passthrough.cpp,bm_demosaic_bilinear.cpp,bm_output_heif_sdr.cpp,main.cpp}`; `.github/workflows/build-and-test.yml`.

**Estimated scope:** M (new target + 2 vcpkg + 3 cases + CI hookup).

---

#### T18 — 18-node + planner + encode microbenches

**Description.** Extend `cpipe-microbench` to ~25 cases: all 18 classic nodes + `Scheduler::dispatch_node` + `MemoryPlanner::plan_graph_coloring` + `PrecisionPlanner::auto_insert` + DNG OpcodeList2 / OpcodeList3 parsing + HEIF SDR / HDR (PQ) encoding. Each case fixed-input (small synthetic Bayer / RGB / display-referred buffers). Output JSON contains per-case wall_ns, cpu_ns, gpu_ns (when Vulkan available), memory_bytes. CPU smoke runs all 25 cases in CI; GPU cases skip on no-Vulkan hosts.

**Acceptance criteria:**

- [ ] All 25 cases listed under `cpipe-microbench --benchmark_list_tests`.
- [ ] CPU smoke completes in <60 s on a 4-core CI runner.
- [ ] GPU-eligible cases (RCD Vulkan path) emit valid numbers on the dev RTX host.

**Verification:**

- [ ] `cpipe-microbench --benchmark_format=json | jq '.benchmarks | length'` returns 25.
- [ ] CI artifact `bench-smoke-<commit>.json` covers all 25 cases (with GPU cases recording "skipped").

**Dependencies:** T17.

**Files likely touched:** `bench/microbench/bm_*.cpp` (22 new files); `bench/CMakeLists.txt`.

**Estimated scope:** L (~22 microbench cases — each is a small fixture wrapper around an already-tested node).

---

#### T19 — `cpipe bench` CLI in `cpipe-cli` (end-to-end pipeline timing)

**Description.** Implement the `cpipe bench` CLI body per architecture §10: `cpipe bench --pipeline <pipe.json> --corpus <dir> [--out report.json]`. Consumes the 50-image v0.4 corpus from T15; produces per-pipeline wall_ms / cpu_ms / gpu_ms / npu_ms / peak_rss / peak_vram numbers. Output appends to `bench/results/<commit>.json`. RD-15 governs (recorded-not-gated).

**Acceptance criteria:**

- [ ] `cpipe bench --pipeline examples/pipelines/full-classic-pipeline.cpipe.json --corpus tests/corpus/ --out /tmp/bench.json` runs end-to-end.
- [ ] Output JSON merges into the existing `bench/results/<commit>.json` (no overwrite of microbench section).
- [ ] Peak RSS measurement uses `getrusage(RUSAGE_SELF, &ru); ru.ru_maxrss`.

**Verification:**

- [ ] `ctest -R test_cpipe_bench_end_to_end` green.
- [ ] Local: `cpipe bench ... --out /tmp/bench.json` produces a valid JSON.

**Dependencies:** T18.

**Files likely touched:** `apps/cli/bench_command.cpp`; `src/cpipe/runtime/PerfCollector.cpp`; `tests/unit/test_cpipe_bench_end_to_end.cpp`.

**Estimated scope:** M (CLI body + perf-collector + 1 test).

---

### Checkpoint E — after T17–T19

- [ ] All three tasks merged; `main` is green.
- [ ] `cpipe-microbench` runs 25 cases on CI; GPU cases run on dev RTX.
- [ ] `cpipe bench` records end-to-end pipeline timings.
- [ ] DoD #4 satisfied: microbench baseline recorded for 18 nodes + planner + encode.
- [ ] No regression on Phase 3.A / Phase 3.B / Phase 3.C / Phase 3.D.

---

### Phase 3.F — Dashboard (2 tasks)

#### T20 — `apps/dashboard/` scaffold + `vega-embed` panels

**Description.** Author the npm workspace under `apps/dashboard/`. Vite 6 + light React + `vega-embed`. Reads `bench/results/*.json` at build time (Vite plugin globs the files). Authors a small panel registry (one panel per metric) using Vega-Lite specs in `apps/dashboard/src/specs/*.json`. Build emits a static SPA into `apps/dashboard/dist/`.

**Acceptance criteria:**

- [ ] `cd apps/dashboard && npm ci && npm run build` produces `apps/dashboard/dist/index.html`.
- [ ] Local: `npm run dev` boots and renders an empty dashboard.
- [ ] `bench/results/*.json` files are picked up by the Vite plugin and inlined.

**Verification:**

- [ ] `cd apps/dashboard && npm run test` green.

**Dependencies:** Checkpoint E.

**Files likely touched:** `apps/dashboard/{package.json,vite.config.ts,tsconfig.json,index.html,src/{index.tsx,App.tsx,specs/*.json,components/*.tsx},test/}`.

**Estimated scope:** M (npm scaffold + 1 Vite plugin + 1 minimal panel + 2 tests).

---

#### T21 — 6-metric trend panels + per-commit drill-down + 90-day window

**Description.** Author six Vega-Lite specs (`apps/dashboard/src/specs/`): `psnr.json`, `ssim.json`, `lpips.json`, `delta_e2000.json`, `wall_ms.json`, `peak_rss.json`. Each renders a 90-day trend line keyed on commit date. Clicking a point opens a detail pane with the per-commit JSON (raw numbers + GitHub commit link). The dashboard root page shows a navigation grid linking to the six panels.

**Acceptance criteria:**

- [ ] All six panels render against `bench/results/baseline.json` + at least 2 follow-up commits.
- [ ] Per-commit detail pane shows raw numbers + GitHub commit link.
- [ ] 90-day window respected; older commits accessible via a "show all" toggle.

**Verification:**

- [ ] `cd apps/dashboard && npm run test` green.
- [ ] Local: `npm run dev` shows the six panels rendering live.

**Dependencies:** T20.

**Files likely touched:** `apps/dashboard/src/specs/{psnr,ssim,lpips,delta_e2000,wall_ms,peak_rss}.json`; `apps/dashboard/src/components/{MetricPanel.tsx,CommitDetail.tsx,Navigation.tsx}`; `apps/dashboard/test/`.

**Estimated scope:** M (6 Vega specs + 3 components + 4 tests).

---

### Checkpoint F — after T20–T21

- [ ] Both tasks merged; `main` is green.
- [ ] `apps/dashboard/` builds; renders all six trend panels.
- [ ] Per-commit drill-down works; GitHub commit links resolve.
- [ ] 90-day window honored.

---

### Phase 3.G — Pages deploy + E2E smoke + tag (2 tasks)

#### T22 — `pages.yml` workflow + GitHub Pages enablement

**Description.** Author `.github/workflows/pages.yml`. Trigger on `main` push when paths match `apps/web/**`, `apps/dashboard/**`, `bench/results/**`, or the workflow file itself. Steps: (1) checkout + Node setup; (2) build editor (`cd apps/web && npm ci && npm run build`); (3) build dashboard (`cd apps/dashboard && npm ci && npm run build`); (4) assemble combined deploy tree (`gh-pages/` with `editor/` at root and `dashboard/` subpath); (5) deploy via `actions/deploy-pages@v5`. GitHub Pages settings enabled in the repo.

**Acceptance criteria:**

- [ ] First main-push after T22 deploys the editor to `https://<user>.github.io/cpipe/` and dashboard to `https://<user>.github.io/cpipe/dashboard/`.
- [ ] Subsequent main pushes that don't touch `apps/{web,dashboard}/**` or `bench/results/**` skip the build but still verify.

**Verification:**

- [ ] `gh workflow run pages.yml` on a follow-up commit emits 0; deploy summary shows the two paths.
- [ ] `curl https://<user>.github.io/cpipe/index.html` returns 200.

**Dependencies:** Checkpoint F.

**Files likely touched:** `.github/workflows/pages.yml`; `.github/workflows/microbench-record.yml` (workflow_dispatch only).

**Estimated scope:** M (1 deploy workflow + 1 manual upload workflow + Pages settings).

---

#### T23 — Playwright E2E smoke + `v0.4` tag

**Description.** Author `tests/e2e/editor_serve_iqa.spec.ts` + `tests/e2e/offline_round_trip.spec.ts` (Playwright). The smoke covers DoD #1 + DoD #2 end-to-end: (a) Playwright spawns `cpipe serve --pipeline examples/pipelines/full-classic-pipeline.cpipe.json --port 4747 --editor-static apps/web/dist`; (b) navigates to `http://localhost:4747/editor/`; (c) waits on a `data-testid="initial-thumbnail-ready"` signal; (d) mutates `tone.filmic_rgb.ev`; (e) waits for an updated thumbnail event; (f) saves via the FSA fallback; (g) reloads; (h) deep-equals the in-memory graph. Lands as a `e2e` job in `build-and-test.yml`. Closes the phase doc §12; updates [`roadmap.md §6`](roadmap.md#6-phase-3--editor--quality-harness-tag-v04) to "shipped"; updates [`README.md`](../README.md) "Current Status"; updates [`architecture.md §17 Q8`](architecture.md#17-open-questions) to "Resolved in P3-PD-12 + P3-PD-47". Tags `v0.4` and drafts the GitHub Release.

**Acceptance criteria:**

- [ ] `tests/e2e/editor_serve_iqa.spec.ts` green on the dev host and in CI.
- [ ] `tests/e2e/offline_round_trip.spec.ts` green on the dev host and in CI.
- [ ] §12 "What Shipped / What Slipped" filled in this phase doc.
- [ ] roadmap.md §6 P3 row updated to "shipped"; README "Current Status" mirrors.
- [ ] architecture.md §17 Q8 → "Resolved in P3-PD-12 + P3-PD-47".
- [ ] `git tag --list 'v0.4'` returns `v0.4`; GitHub Release notes attached.

**Verification:**

- [ ] `ctest -L e2e --output-on-failure` green (both specs).
- [ ] `ctest --preset linux-debug --output-on-failure` green.
- [ ] `ctest --preset linux-release-clang --output-on-failure` green.
- [ ] `gh run list --workflow=build-and-test.yml --branch=main --limit=5` shows 5 green.
- [ ] `gh run list --workflow=pages.yml --branch=main --limit=2` shows 2 green.

**Dependencies:** T22.

**Files likely touched:** `tests/e2e/{playwright.config.ts,editor_serve_iqa.spec.ts,offline_round_trip.spec.ts}`; `.github/workflows/build-and-test.yml` (e2e job); `docs/roadmap.md` §6; `docs/architecture.md` §17; `README.md`; `docs/phase-03-editor-iqa.md` §12.

**Estimated scope:** M (2 Playwright specs + CI e2e job + 3 doc updates + tag + release).

---

### Checkpoint G — P3 DoD

- [ ] §11 verification commands all green.
- [ ] Latest `main` CI green on the release commit (P3-PD-4 waiver).
- [ ] No regressions on P1 / P2 unit / integration tests.
- [ ] `v0.4` tag pushed; GitHub Release published.
- [ ] `gh-pages` branch has editor at `/` and dashboard at `/dashboard/`.

---

## 8. Architecture Notes (P3-specific)

- **Editor server lives inside `cpipe-cli`.** Per [architecture §4](architecture.md#4-process-and-thread-model), the editor server is *not* a separate binary — it runs inside the CLI process. T3 wires `cpipe-cli` to depend on `cpipe-server` (static lib) and the `cpipe serve` subcommand starts the uWebSockets event loop on a dedicated I/O thread. Heavy work (thumbnail downsample + WebP encode) is enqueued onto the TaskFlow executor so the I/O loop never blocks.
- **Thumbnail tap point lives in `Scheduler::dispatch_node`.** After a node's `process()` completes and the host freezes its output `BufferMetadata`, the scheduler checks subscribers and (if any) enqueues a downsample + encode task. The tap is inert when no subscriptions exist — no perf cost in batch-CLI mode.
- **Editor server is single-active-pipeline.** PUT `/api/pipelines/active` replaces; the server keeps one `PipelineSession` instance. Concurrent editor connections share the same session; param updates from each client race with last-write-wins semantics — a warning banner in the editor surfaces when a second connection joins.
- **OCIO Vulkan compute path (T1).** `OcioVulkanProcessor` constructs by calling `OCIO::Config::CreateFromFile` + `OCIO::GpuShaderDesc::CreateShaderDesc(OCIO::GPU_LANGUAGE_GLSL_VK_4_6)` per [P3-PD-51](#4-phase-decisions-p3-pd-n). The emitted GLSL is compiled to SPIR-V via glslang at runtime. The cache key is `(config_path, src_cs, dst_cs)`; cache hits return a pre-bound `VkComputePipeline` + descriptor set layout. Inputs / outputs are VMA-backed Vulkan images; v0.2 scene-display processors are texture-free per [P3-PD-53](#4-phase-decisions-p3-pd-n).
- **Pipeline-v0.4 `ui` is editor-only.** `Pipeline::load` parses `ui` but does not store it inside `Pipeline`. The editor server's GET `/api/pipelines/active` echoes back the on-load JSON including `ui` (so a fresh editor connecting mid-edit gets the layout). When the editor saves through PUT `/api/pipelines/active`, it includes `ui` again.
- **Schema sync uses `If-Modified-Since`.** The editor includes the cached schema's `Last-Modified` timestamp on subsequent fetches; the server responds 304 if unchanged. This keeps the 7-day cache from refetching on every page load.
- **Python sidecar locality.** `cpipe iqa` spawns `python -m cpipe_iqa` per metric batch (not per image) to amortize the venv startup. The sidecar reads EXR / HEIF via OpenImageIO-python and computes metrics in-process. License isolation is by process boundary.
- **Reverting any P3 schema changes.** v0.3 → v0.4 migration is lossless (only adds optional `ui`); the v03_to_v04 migration script (T0) is a one-line `version: 0.4` injection. v0.4 → v0.3 downgrade is also lossless (drop `ui`) if needed mid-phase.

---

## 9. Tests in P3 (additions to P2)

| # | Test | Layer | Asserts |
|---|------|-------|---------|
| 65 | `test_pipeline_v0_4_loader` | unit | v0.4 accept, v0.3 reject, `ui` optional, range validation per param |
| 66 | `test_editor_protocol_schema` | unit | REST envelope + WS frame format schema validation |
| 67 | `test_editor_server_health` | unit | GET `/api/health` returns expected envelope |
| 68 | `test_editor_server_rest` | unit | 8 endpoints + envelope shape + error paths |
| 69 | `test_editor_server_ws` | unit | WS handshake, binary frame round-trip, ack frame |
| 70 | `test_thumbnail_subscription` | unit | subscribe → encode → push; multi-subscriber cap |
| 71 | `test_cpipe_serve_cli` | unit | `--port`, `--bind`, `--pipeline`, `--editor-static`, LAN warning |
| 72 | `test_ocio_vulkan_processor` | unit | CPU↔GPU result match; cache hit; LUT upload |
| 73 | `test_node_<8>_params` | unit | 8 nodes with new live params (1 case file each, 12+ assertions per file) |
| 74 | `test_install_editor_static` | unit | CMake install rule deposits editor into share/cpipe/editor |
| 75 | `test_iqa_cpp_metrics` | unit | PSNR / SSIM / MS-SSIM / ΔE2000 against piq within tolerance |
| 76 | `test_iqa_subprocess` | unit | `cpipe iqa` spawns sidecar; 2-image canary |
| 77 | `test_iqa_corpus_run` | integration | full 50-image corpus run; emits baseline JSON |
| 78 | `test_cpipe_bench_end_to_end` | unit | `cpipe bench` produces valid JSON, peak RSS reported |
| 79 | `cpipe-microbench` | bench | 25 cases listed; CPU smoke completes |
| 80 | `tests/e2e/editor_serve_iqa.spec.ts` | e2e | DoD #1: filmic_rgb.ev mutation → thumbnail update |
| 81 | `tests/e2e/offline_round_trip.spec.ts` | e2e | DoD #2: FSA + fallback round-trip |
| 82 | `apps/web/test/` | frontend unit | Vitest: store + components + IPC |
| 83 | `apps/dashboard/test/` | frontend unit | Vitest: panel rendering + spec validation |

| Label | Tests | Purpose |
|-------|-------|---------|
| `unit` | 65–76 | per-component invariants |
| `iqa` | 77 | corpus-level IQA run |
| `bench` | 79 | microbench smoke (CPU) |
| `e2e` | 80, 81 | Playwright editor + server |
| `frontend` | 82, 83 | Vitest in npm workspaces |
| `vulkan` | 72 | conditional on `CPIPE_VULKAN_AVAILABLE` |

P2's tests 30–64 continue to pass; the per-node golden harness (P2 test 64) continues to gate the 7 RT-carry nodes as deterministic self-references per P3-PD-55.

---

## 10. Risk Register (P3-only)

| # | Risk | Impact | Likelihood | Mitigation |
|---|------|--------|-----------|------------|
| P3-R1 | Playwright E2E flaky in CI — race between `cpipe serve` ready, schema fetch, first thumbnail. | High | Medium | T23 adds a `/api/health` readiness probe; Playwright waits on `data-testid="initial-thumbnail-ready"` before asserting; retry once on failure. |
| P3-R2 | Upstream corpus fetch blocked — RAISE / Kalantari behind academic mirrors, FiveK GDrive throttling. | High | Medium | T14 manifest carries multiple mirrors + a cpipe-mirror fallback (CC0-only). CI cache reduces repeat fetch. If a dataset is unreachable, swap to a license-compatible alt + log in `tests/corpus/SUBSTITUTIONS.md`. |
| P3-R3 | C++ in-binary PSNR/SSIM/MS-SSIM/ΔE2000 numerically differ from piq reference. | Medium | High | T13 cross-checks against piq via subprocess on a 5-image canary; tolerates ≤0.05 dB PSNR / 0.001 SSIM / 0.002 MS-SSIM / 0.1 ΔE2000 delta; documents differences in `tools/iqa/README.md`. |
| P3-R4 | RTX thumbnail encode budget busts wall-time when many nodes subscribe. | Medium | Low | T6 limits to ≤4 concurrent subscribers; degrades to 2 fps at >4; profiles via Tracy `EditorServer::push_thumbnail` span. |
| P3-R5 | pipeline-v0.4 `ui` migration silently breaks an external test pipeline. | Medium | Low | T0 prints a one-line "schema upgraded v0.3 → v0.4 (added optional ui)" hint when loading legacy fixtures; `Pipeline::load` rejects unmigrated v0.3 with a clear error pointing at `tools/migrate/v03_to_v04.py`. |
| P3-R6 | OCIO GLSL emitted by OCIO 2.4 doesn't compile through glslang on RTX driver. | Medium | Low | T1 pre-tests with a 2-colorspace synthetic config; fallback path stays the P2 CPU processor; if compile fails, T1 records an alternate carry (recompile OCIO GLSL externally, ship `.spv` alongside `config.ocio`). |
| P3-R7 | 50-image corpus disk footprint (~3-5 GB) bloats developer setups. | Low | Medium | `scripts/prepare_corpus.py --subset minimal` (10 images, ~600 MB) for quick local runs; full corpus only in CI / before release. |

Inherited risks ([Research 00 §7](research/00-summary.md#7-risk-register)) carried into P3: **R9** (Chrome 142 LNA UX — irrelevant for plaintext HTTP); **R15** (pyiqa CC-BY-NC-SA — handled by subprocess). P2-R4 / P2-R6 are explicitly carried as [P3-PD-46](#4-phase-decisions-p3-pd-n) items, not retired in P3.

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

# 6. Per-node golden (18 P2 nodes + 7 RT-carry self-references, PSNR ≥ 40 dB)
ctest --preset linux-release-clang -L golden --output-on-failure

# 7. IQA canary (5-image subset against bench/results/baseline.json)
ctest --preset linux-release-clang -L iqa --output-on-failure

# 8. E2E smoke (Playwright)
ctest --preset linux-release-clang -L e2e --output-on-failure

# 9. SDR / HDR / QBC CLI smokes (carry from P2 §11 steps 7–9)
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro.dng \
    -p examples/pipelines/full-classic-pipeline.cpipe.json \
    -o /tmp/cpipe_p3_sdr.heif
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro.dng \
    -p examples/pipelines/full-classic-pipeline-hdr.cpipe.json \
    -o /tmp/cpipe_p3_hdr.heif
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/corpus/pixel8pro-qbc.dng \
    -p examples/pipelines/full-classic-pipeline.cpipe.json \
    -o /tmp/cpipe_p3_qbc.heif

# 10. Python sidecar smoke
python -m cpipe_iqa --image-a /tmp/cpipe_p3_sdr.heif --image-b /tmp/cpipe_p3_sdr.heif \
                    --metrics psnr,ssim --report json

# 11. Corpus IQA baseline run
./build/linux-release-clang/src/cpipe/cli/cpipe iqa \
    examples/pipelines/full-classic-pipeline.cpipe.json \
    tests/corpus/ \
    --baseline bench/results/baseline.json

# 12. Microbench end-to-end
./build/linux-release-clang/bench/cpipe-microbench \
    --benchmark_format=json \
    --benchmark_out=bench/results/$(git rev-parse HEAD).json

# 13. Server smoke
./build/linux-release-clang/src/cpipe/cli/cpipe serve --port 4747 &
sleep 1
curl -s http://localhost:4747/api/health | jq .ok    # → true
kill %1

# 14. CI status (5 green build-and-test, 2 green pages)
gh run list --workflow=build-and-test.yml --branch=main --limit=5
gh run list --workflow=pages.yml --branch=main --limit=2

# 15. Tag
git tag -a v0.4 -m "cpipe v0.4 — Editor + Quality Harness"
git push origin v0.4

# 16. GitHub Release
gh release create v0.4 --verify-tag --generate-notes --title "cpipe v0.4 - Editor + Quality Harness"
```

If commands 1–16 all return zero exit status and latest `main` CI is green on the release-candidate commit, P3 is done. The 24-hour bake remains waived per P3-PD-4.

---

## 12. What Shipped / What Slipped

*Filled in by T23 when the closing PR pushes `v0.4`.*

**What shipped.** *TBD at T23.*

**What slipped.** *TBD at T23. Expected carries per [P3-PD-46](#4-phase-decisions-p3-pd-n): real Pixel / alt-phone QBC corpus → v1.1; direct Halide Vulkan AOT command-buffer handoff → v1.1; binary Tracy capture in CI → evidence-only.*

---

## 13. Dependencies (vcpkg.json + npm + Python)

**vcpkg additions in P3:**

- `uwebsockets` — HTTP / WS server library, Apache 2.0.
- `glslang` — GLSL → SPIR-V compiler (used by OCIO Vulkan in T1), Apache 2.0.
- `libwebp` — WebP encoder for thumbnails (T6), BSD.
- `google-benchmark` — microbench library (T17), Apache 2.0.
- `nanobench` — kernel A/B microbench library (T17), MIT.

No new FetchContent in P3. Halide v21 stays on FetchContent (carry [P0-PD-31](phase-00-foundation.md) / [P1-PD-7](phase-01-walking-skeleton.md#4-phase-decisions-pd-n) / [P2-PD-59](phase-02-classic-nodes-hdr.md#4-phase-decisions-p2-pd-n)).

**npm additions (apps/web/):**

- `@xyflow/react` 12.x
- `react` 19.x, `react-dom` 19.x
- `zustand` 5.x
- `ajv` 8.x
- `vite` 6.x
- `typescript` 5.5.x
- `vitest` 2.x
- `@playwright/test` 1.x

**npm additions (apps/dashboard/):**

- `react` 19.x, `react-dom` 19.x
- `vega`, `vega-lite`, `vega-embed`
- `vite` 6.x
- `typescript` 5.5.x
- `vitest` 2.x

**Python additions (tools/iqa/pyproject.toml):**

- `piq >= 0.8` (Apache 2.0)
- `pyiqa >= 0.1.10` (CC-BY-NC-SA — subprocess-isolated)
- `numpy`
- `OpenImageIO` (Python binding to OIIO, BSD)

All Python deps pinned per `v0.4`; CI installs via `pip install ./tools/iqa[ci]`.

---

## 14. Open Questions

P3 does not open new global open questions. The following local items are tracked here; if any becomes a hard blocker, the maintainer escalates by adding a new P3-PD row:

- Should `cpipe serve --pipeline <file>` auto-reload on file mtime change? Default v0.4 = no; defer.
- Manifest URL pattern for the corpus mirror (CDN vs raw GitHub) — T14 picks.
- Whether `cpipe iqa` default report format is Markdown (human) or JSON (machine). T13 picks JSON; `--report md` always available.
- Whether Playwright E2E should ship a "record video" job for PR failure forensics — T23 evaluates; defer if CI minutes spike.
- Whether `apps/dashboard/` PR delta query string (`?compare=base...head`) lands in P3 or P5 — defer to P5 polish per P3-PD-30 unless trivial.

The following [Architecture §17](architecture.md#17-open-questions) global open questions are touched by P3:

- **Q8** (Desktop-only / offline mode for the editor) — **Resolved in P3-PD-12 + P3-PD-47** (T11 implementation + RD-10 closure).

The 9 remaining global open questions (Q2, Q3, Q4, Q5, Q7, Q9, Q11, Q13, Q14) are not in P3 scope; they remain tracked in [architecture.md §17](architecture.md#17-open-questions).

---

## 15. Out of Scope (P3)

Stated explicitly so contributors don't accidentally expand P3:

- Noise XK pairing / TLS / WSS / LNA / WebRTC / TURN — v1.1 ([RD-8](roadmap.md#1-decision-quick-reference)).
- HDR-aware browser preview (UltraHDR JPEG in Chrome 142) — v1.1.
- Subgraph collapse / group-as-node UI ([Research 11 §3.5](research/11-pipeline-editor-and-connectivity.md)) — v1.2.
- mDNS / discovery service (RD-8 / Research 11 §4.9).
- `cpipe model` CLI verb — P4 (needs ExecuTorch + model fetcher).
- AI ISP nodes (NAFNet, AdaInt, Wronski) — P4.
- ExecuTorch + ONNX Runtime + QAIRT integration — P4.
- Multi-frame `BatchedBuffer` — P4.
- HDR+ burst align / merge / finish (true burst) — P4.
- Camera2 / Android target — v1.1.
- macOS / iOS targets — v1.2 / v2.
- Adobe DNG SDK ingest — cancelled (RD-11).
- Hexagon / Metal device planes — v1.1 / v2.
- Direct 4×4 Quad Bayer demosaic (AI or classic) — v1.2.
- X-Trans demosaic — D12 / v1.2.
- Real Pixel / alt-phone QBC corpus — carry to v1.1 ([P3-PD-46](#4-phase-decisions-p3-pd-n)).
- Direct Halide Vulkan AOT command-buffer handoff — carry to v1.1 ([P3-PD-46](#4-phase-decisions-p3-pd-n)).
- Binary Tracy capture in CI — local-only evidence, carry to v1.1 ([P3-PD-46](#4-phase-decisions-p3-pd-n)).
- Lensfun lens corrections — v1.1.
- 12-bit HEVC Main12 HEIF — v2.
- NVENC HEVC encoder — v1.1+ / v2.
- Handwritten SPIR-V dispatch ops (non-Halide GPU nodes) — P5+ if needed.
- JPEG output / JPEG fallback — D17 forbids.
- DNG re-export — v2.
- Vendor-specific OpcodeList opcodes — v2.
- Editor-side authoring of new node types — Q15 resolved no (v1).
- External `.so` plugin loading — D4 reservation; v2.
- Mertens HDR-pyramid variant — v1.1.
- ACES 2.0 Output Transform fidelity (cg-config-v4.0.0_aces-v2.0_ocio-v2.5) — P3+ via OCIO config update (deferred from P2).
- AMaZE / BM3D GPU AOT — P3+ if microbench justifies (P2 carry).
- HDR-VDP-3 IQA metric in CI — nightly only via pyiqa subprocess; gated weekly.

---

## 16. See Also

- [`roadmap.md §6`](roadmap.md#6-phase-3--editor--quality-harness-tag-v04) — P3 phase row + RD-NN decisions.
- [`phase-02-classic-nodes-hdr.md`](phase-02-classic-nodes-hdr.md) — preceding phase doc; P2-PD references throughout.
- [`phase-01-walking-skeleton.md`](phase-01-walking-skeleton.md) — P1-PD-69 / P1-PD-70 references for the RT-retire scope.
- [`phase-00-foundation.md`](phase-00-foundation.md) — initial repo / CI / ABI skeleton.
- [`architecture.md`](architecture.md) — six-target layout, threading model, lifecycle; §8 editor protocol; §17 Q8 resolved here.
- [`buffer.md`](buffer.md) — `BufferMetadata` per-output `MetadataBuilder` flow; thumbnail tap uses freeze point.
- [`plugin-sdk.md`](plugin-sdk.md) §3 — `cpipe_compute_suite_v1` (P2 carry, P3 unchanged); §7 manifest schema (`params` block used by P3-PD-37 8 nodes).
- [`research/09-image-quality-benchmarks.md`](research/09-image-quality-benchmarks.md) — IQA library + metric matrix; harness skeleton; pyiqa license posture.
- [`research/11-pipeline-editor-and-connectivity.md`](research/11-pipeline-editor-and-connectivity.md) — React Flow 12 case; editor architecture; per-node UI; connectivity tiers (only tier 4 used in P3).
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — scheduler tap point for thumbnail subscriptions.
- [`tech.md`](tech.md) — license inventory for new vcpkg deps.
