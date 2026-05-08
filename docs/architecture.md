# cpipe System Architecture

> Date: 2026-05-08 · Companion: [`tech.md`](tech.md) · Detail: [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md), [`research/00-summary.md`](research/00-summary.md)

This document explains **how cpipe is assembled** for v1: the modules, the process layout, the threading model, the cross-platform split, the editor protocol surface, the lifecycle of a pipeline run, and the configuration / testing / observability concerns that cut across them. The companion [`tech.md`](tech.md) lists every external dependency and version pin; this document is silent on library choice except where the choice shapes a module boundary. The detailed designs of the buffer subsystem and plugin ABI live in [`buffer.md`](buffer.md) and [`plugin-sdk.md`](plugin-sdk.md) — this document references rather than restates them.

The structure is layered: external interfaces → runtime internals → cross-platform split → build & release. Read top-down for orientation, then drill into the chapter-level documents for evidence.

---

## 1. System View

cpipe is a single C++20 native runtime that runs on Linux desktop and Android, plus a static React Flow editor served from GitHub Pages. There are two **deliverables** in v1 and they share most code:

```
                ┌─────────────────────────────────┐
                │  cpipe runtime  (C++20, native) │
                │                                 │
   DNG file ──▶ │  Ingest ─▶ Pipeline DAG ─▶ HEIF │ ──▶ HEIF file
                │             │                   │
   Camera2 ───▶ │  Ingest ─▶  │       (Android)   │
   AHB burst    │             │                   │
                │             ▼                   │
                │       Editor Server  (HTTP+WS)  │
                └────────────┬────────────────────┘
                             │  WebSocket / WebRTC
                             ▼
                ┌─────────────────────────────────┐
                │  Web Editor  (React Flow)       │
                │  hosted on GitHub Pages         │
                └─────────────────────────────────┘
```

There is no mandatory network connection; the editor is optional. The runtime's default mode is offline batch (`cpipe run input.dng -p pipe.json -o out.heif`).

The CLI, the Android service, and the editor backend all link the same `cpipe-runtime` library. The differences are:

- **Linux CLI** (`apps/cli/`) — invokes `Pipeline::run()` synchronously over a `DngReader`-fed buffer, then exits.
- **Android app** (`apps/android/`) — wraps `cpipe-runtime` as a JNI library; Compose UI on top; Camera2 NDK supplies bursts via `Camera2BufferProducer`.
- **Editor server** is **embedded inside whichever process owns the pipeline** (CLI or Android). It is not a separate binary.

The web editor is a static SPA; its only persistent state is `localStorage`. It speaks REST + WebSocket to whatever runtime instance the user has paired with (typically a developer laptop or their own phone over LAN).

For the data-flow walkthrough at the chapter level, see [Research 00 §3](research/00-summary.md#3-architecture-overview).

---

## 2. Repository Layout

```
cpipe/
├── apps/
│   ├── cli/             # Linux CLI executable: cpipe-cli target
│   ├── android/         # Gradle project; AGP 8.7 + NDK r27; Compose UI
│   └── web/             # Vite + React + TS; GitHub Pages deploy
├── src/
│   └── cpipe/
│       ├── core/        # PixelFormat, BufferLayout, IBuffer, status codes
│       ├── runtime/     # Pipeline, Scheduler, DevicePlane, ComputeContext
│       │   └── platform/{linux,android,apple}/  # platform-conditional sources
│       ├── sdk/         # public C++ SDK; consumed by built-in & external nodes
│       ├── nodes/       # built-in nodes (demosaic, WB, tone, denoise, AI…)
│       ├── ingest/      # DngReader, Camera2BufferProducer
│       ├── color/       # OCIO + lcms2 wiring; HEIF writer
│       └── server/      # HTTP+WS editor server (uWebSockets)
├── tests/
│   ├── unit/            # Catch2 unit tests
│   ├── golden/          # per-node EXR fixtures (Git LFS)
│   ├── iqa/             # piq + pyiqa harness
│   └── corpus/          # 50-image v1 corpus (Git LFS)
├── bench/               # Google Benchmark + nanobench microbench suite
├── cmake/               # CMakePresets.json fragments, helpers, FindFoo.cmake
├── vcpkg.json           # manifest mode, builtin-baseline pinned per release
├── vcpkg/overlay-ports/ # local patches for kvazaar, libultrahdr 1.4, …
├── scripts/             # fetch_models.py, prepare_corpus.py, etc.
├── models/              # populated by scripts; gitignored; SHA256 manifest committed
├── docs/                # research/, buffer.md, plugin-sdk.md, tech.md, this file
└── CMakePresets.json    # top-level build presets
```

The `apps/web/` subtree is a self-contained npm workspace; everything else is one CMake project rooted at the repository top-level.

---

## 3. Native Module Decomposition

cpipe-runtime is split into six CMake targets so that consumers (CLI, Android shared library, future plugins) can pull in the minimum surface they need. All targets ship under one CMake project.

| Target | Kind | Depends on | Public surface |
|--------|------|------------|----------------|
| `cpipe-core` | static | — | `PixelFormat`, `BufferLayout`, `IBuffer`, status codes; no Vulkan/Metal/Halide types |
| `cpipe-runtime` | static | `cpipe-core`, VMA, Vulkan, TaskFlow, OCIO, lcms2, spdlog, Tracy (opt) | `Pipeline`, `Scheduler`, `DevicePlane`, `ComputeContext`, `InferenceContext` |
| `cpipe-sdk` | header-only | `cpipe-core` | `cpipe::sdk::Node`, `Buffer`, `ComputeContext`, `ParamView`, `CPIPE_REGISTER_NODE` |
| `cpipe-builtin-nodes` | static | `cpipe-sdk`, Halide AOT static libs (one per node), Slang `.slangbin` data, `cpipe-runtime` | the 18 classic + 3 AI nodes — registered through `CPIPE_REGISTER_NODE` |
| `cpipe-server` | static | `cpipe-runtime`, uWebSockets, libsodium, noise-c, nlohmann-json | `EditorServer` (REST + WS) |
| `cpipe-cli` | executable | `cpipe-runtime`, `cpipe-builtin-nodes`, `cpipe-server`, CLI11 | the `cpipe` binary |

Android's shared library `libcpipe.so` is a dedicated `cpipe-android` target that links the same five static libraries plus a JNI shim. macOS / iOS (v2) follows the same pattern with `cpipe-apple`.

The boundary between **`cpipe-core`** and **`cpipe-runtime`** is deliberate: external plugins (v2) only depend on `cpipe-core` (and `cpipe-sdk`, header-only), so they never need to know about Vulkan, Halide, OCIO, or any backend SDK. The `cpipe-sdk` headers reference only `cpipe-core` types and the C ABI.

---

## 4. Process and Thread Model

cpipe is a **single-process** runtime. The CLI and the Android service each own one process; the editor server runs *inside* that process, not as a separate daemon.

```
                       cpipe process (CLI or Android)
   ┌───────────────────────────────────────────────────────────────┐
   │                                                                │
   │   Main thread                                                  │
   │     ─ parses CLI args, loads pipeline.json, drives Pipeline    │
   │                                                                │
   │   ┌───────────────────────────────────────────────────────┐    │
   │   │  TaskFlow Executor (process-wide, N = HW threads − 1) │    │
   │   │   ─ runs node `process()` callbacks                    │    │
   │   │   ─ Halide custom_par_for is bound to this executor    │    │
   │   └───────────────────────────────────────────────────────┘    │
   │                                                                │
   │   DevicePlane (one instance per backend)                       │
   │     ─ Vulkan: 1 VkQueue per family + per-thread cmd pools      │
   │     ─ Metal (v2): 1 MTLCommandQueue                            │
   │     ─ Hexagon HTP: QAIRT context per model                     │
   │                                                                │
   │   I/O thread pool (uWebSockets event loop, 1 thread)           │
   │     ─ accepts WS frames; dispatches to executor for heavy work │
   │                                                                │
   │   Camera2 callback thread (Android only)                       │
   │     ─ AImageReader_acquireNextImageAsync → BurstFrame queue    │
   │                                                                │
   └────────────────────────────────────────────────────────────────┘
```

Key design rules ([Research 03](research/03-heterogeneous-scheduler.md)):

- **One TaskFlow `Executor` per process**, sized to `std::thread::hardware_concurrency() - 1`. Halide is told to use it via `halide_set_custom_do_par_for` so it never spawns its own competing thread pool.
- The **device plane** is *not* a separate executor. Vulkan submissions, Hexagon HTP runs, and Metal command buffers are issued from whichever TaskFlow worker is currently running the originating node. Synchronization between devices is via the timeline objects defined in [Buffer §8](buffer.md#8-synchronization-host-only).
- The **I/O thread** runs the uWebSockets event loop in its own thread. Heavy work (thumbnail downsample, profile JSON serialize) is enqueued onto the executor so the I/O loop never blocks.
- A single `Pipeline` instance is **not reentrant** — concurrent runs require separate `Pipeline` objects. The plugin ABI guarantees that a single node instance never sees concurrent `process()` calls ([Plugin SDK §8.5](plugin-sdk.md#85-process)).

---

## 5. Pipeline Lifecycle

A pipeline run goes through five phases. The host owns every step except `process()`.

```
   load JSON → memory plan → precision plan → device plan
                                                  │
                                                  ▼
                                              prepare()  ──▶ JIT-warm Halide / Slang / load models
                                                  │
                                                  ▼
                                              process()  ──▶ scheduler dispatches by topo order
                                                  │
                                                  ▼
                                              teardown   ──▶ destroy() per node; release pool
```

```
   ┌─ Caller ─┐    ┌─ Pipeline ─┐    ┌─ Scheduler ─┐    ┌─ Node ─┐
       │              │                  │                 │
       │─ load(json) ─▶│                  │                 │
       │              │─ validate schema ▶│                 │
       │              │─ topo / mem plan ▶│                 │
       │              │─ precision plan ──▶                 │
       │              │─ device assign ───▶                 │
       │              │                   │─ create() ───────▶│
       │              │                   │◀── per-instance state
       │              │                   │─ prepare() ──────▶│
       │              │                   │◀── ready
       │─ run(in) ────▶│                   │                 │
       │              │─ submit ──────────▶│                 │
       │              │                   │─ process() ─────▶│
       │              │                   │◀── outputs
       │              │◀──────────────────│                 │
       │◀── outputs   │                   │                 │
       │─ destroy ────▶│                   │─ destroy() ─────▶│
```

Notes:

1. **Static topology.** Per [D6](research/_toc.md#1-decisions-locked-before-research), the scheduler compiles the DAG *once* at load. Parameters can change between runs; nodes and edges cannot.
2. **Memory plan.** The scheduler runs the interference-graph coloring algorithm from [Research 03 §5](research/03-heterogeneous-scheduler.md) and pre-allocates intermediates through the `BufferAllocator` ([Buffer §7](buffer.md#7-creation-and-allocation)). If `peak > device cap` the load fails immediately.
3. **Precision plan.** Each node's manifest declares `precision: ["fp16", "fp32"]` per port. The scheduler inserts the minimum number of `precision_convert` nodes; this is invisible to authors ([Plugin SDK §7](plugin-sdk.md#7-manifest-schema)).
4. **Device plan.** Manifests declare per-device implementations (`halide_cpu`, `halide_vulkan`, `slang_metal`, `qnn_htp_v75`, …). The scheduler picks one and inserts `Handoff` markers at cross-device boundaries — the only place the "one copy" budget is allowed (e.g. the AHB → HTP memcpy, [Risk R3](research/00-summary.md#7-risk-register)).
5. **`prepare()`** is where Halide / Slang pipelines are JIT-warmed and inference models are loaded. Optional; default no-op.
6. **`process()`** is the hot path; it submits `submit_halide` / `submit_slang` / `submit_inference` calls. The scheduler waits on input edge timelines before dispatch and signals output edge timelines after ([Buffer §8](buffer.md#8-synchronization-host-only)).
7. **Burst** (`cardinality: "array"`) sees `n` `IBuffer*` inputs; the merge node consumes all and produces a single output ([Buffer §10.3](buffer.md#103-pipeline-inputs--outputs)).

---

## 6. Android Burst Sequence

This is the only mobile-specific flow detailed here; everything else is platform-agnostic.

```
    Compose UI (Kotlin)
        │
        │  capture()                          (cpipe-android JNI)
        ▼                                     ┌──────────────────┐
   Camera2BufferProducer.next_burst(N) ──────▶│  AImageReader   │
        │                                     │  N + 4 max imgs  │
        │  for i = 0..N-1:                    └────────┬─────────┘
        │      AImage_acquireAsync ─▶ AHardwareBuffer  │
        │      VK_ANDROID_external_memory_AHB import   │
        │      sync_file fd → VkSemaphore (binary)     │
        │      wrap as VulkanAHBBuffer                  │
        ▼                                     ┌────────▼─────────┐
   pipeline.run({ "raw_burst": [B0..BN-1] })  │  HDR HEIF output │
        │                                     │  (libheif +       │
        ▼                                     │   kvazaar +       │
   align/merge/finish (HDR+ derivative)       │   libultrahdr)   │
        │                                     └──────────────────┘
        ▼
   color management → tone → denoise → encode → file
```

Detailed contract in [Research 16](research/16-camera2-raw-and-burst.md) and [Buffer §10.2](buffer.md#102-camera2bufferproducer). The acquire-fence FD-to-VkSemaphore conversion happens *inside* `Camera2BufferProducer` so it never crosses the plugin boundary ([Buffer §8](buffer.md#8-synchronization-host-only)).

Each frame becomes one independent `.dng` on disk if the user requested raw archival (Q10 resolution: N independent DNGs, not a multi-IFD container).

---

## 7. Cross-Platform Abstraction

Per [D19](research/_toc.md#1-decisions-locked-before-research), all non-UI code is C/C++. Platform variation is contained in `src/cpipe/runtime/platform/`:

```
src/cpipe/runtime/platform/
├── common/      # protocols and POSIX-portable code
├── linux/       # Vulkan loader path, opaque-fd external memory, X-platform libheif
├── android/     # Vulkan AHB import, AImageReader integration, JNI helpers
└── apple/       # Metal allocator, IOSurface, Core ML — v2; stubs in v1
```

CMake selects the directory through `target_sources` conditional on `CPIPE_PLATFORM`. Headers in `common/` declare interfaces (`IExternalMemoryImporter`, `IPlatformClock`, `IFenceFactory`); implementations in `linux/` / `android/` are linked at build time, not at runtime — there is no `dlopen("platform.so")` mechanism.

UI code lives outside `cpipe-runtime`:

- **Android UI** — Kotlin + Compose under `apps/android/`. JNI surface in `apps/android/src/main/cpp/` exposes `Pipeline`, `Camera2BufferProducer`, and `EditorServer`.
- **Linux CLI** — pure C++20 in `apps/cli/`; CLI11 for argument parsing.

This split lets us evolve the UI tier independently and keeps `cpipe-runtime` JVM-free.

---

## 8. Editor Server Surface

The editor server is a small uWebSockets app inside `cpipe-server`. It serves three roles: serve the manifest schema for editor-side validation, expose pipeline state for inspection / mutation, and stream live preview / profile events.

### REST control plane (JSON)

| Method · Path | Purpose |
|---------------|---------|
| `GET /api/health` | liveness + ABI version |
| `GET /api/schemas/node` | manifest schema (served from embedded resource) |
| `GET /api/schemas/pipeline` | pipeline JSON schema (subset of manifest schema) |
| `GET /api/registry/nodes` | list of registered node IDs and their manifests |
| `GET /api/pipelines/:id` | the loaded pipeline (graph JSON) |
| `POST /api/pipelines/:id/params` | apply a `{node_id, key, value}` parameter delta |
| `POST /api/pipelines/:id/run` | trigger a re-run (returns a run id) |
| `GET /api/pipelines/:id/runs/:run_id` | run status + output paths |
| `POST /api/pair/begin` | start Noise XK pairing; returns QR-encoded token |
| `POST /api/pair/finish` | complete pairing; returns session token |

### WebSocket event plane

After pairing, the client opens `WSS /ws` with a `Sec-WebSocket-Protocol: cpipe.v0.1+session.<token>` header. Frames are framed binary:

```
   1 byte   : type      (0x01 thumbnail, 0x02 profile, 0x03 log, 0x04 ack, 0x10 control)
   4 bytes  : node-id   (registry index, big-endian)
   4 bytes  : timestamp (ms since pipeline start, big-endian)
   4 bytes  : length    (payload bytes, big-endian)
   N bytes  : payload   (WebP for thumbnail, JSON for profile/log/control)
```

Thumbnails are 256×256 WebP (downsampled by the runtime worker, encoded by libwebp). Profile events carry per-node `start/end_ms`, peak memory, and device tag. Control frames are JSON (e.g. `{ "op": "param", "node": "tone", "key": "ev", "value": 0.5 }`) and round-trip the same way the REST `params` endpoint does — they exist so the editor can avoid a request/response per slider tick.

### Connectivity tiers

The editor implements a four-tier fallback for browser-to-runtime contact ([Research 11 §5](research/11-pipeline-editor-and-connectivity.md)):

1. **Public DNS + Let's Encrypt** — cleanest; primary recommendation.
2. **Chrome 142 LNA + WSS** — if the browser supports `targetAddressSpace: "local"`.
3. **WebRTC DataChannel + Cloudflare Workers signalling**.
4. **WebRTC + Cloudflare TURN** — last resort.

Pairing always uses **Noise XK** over a QR-fingerprint and a one-time token. Session keys live in browser `localStorage`; the runtime persists the public key in `~/.config/cpipe/peers.json` so re-pairing is not required.

---

## 9. Configuration

Runtime configuration is layered (lower wins on conflict):

```
   defaults compiled in  ◀──  ~/.config/cpipe/settings.json  ◀──  $CPIPE_*  env  ◀──  CLI flag
```

Example `settings.json`:

```json
{
  "server": { "port": 4747, "bind": "127.0.0.1" },
  "cache":  { "model_dir": "~/.cache/cpipe/models",
              "thumbnail_dir": "~/.cache/cpipe/thumbs" },
  "iqa":    { "thresholds": { "psnr_db": 38.0, "delta_e2000": 2.0 } },
  "tracy":  { "enabled": false }
}
```

Schema lives at `cpipe/schemas/settings-v0.1.json`; validated at startup. Unknown keys log a warning but do not fail.

---

## 10. CLI Surface

```
cpipe run     <input.dng>... -p <pipeline.json> -o <out.heif>
cpipe info    nodes | pipelines | gpu | npu
cpipe bench   --pipeline <pipe.json> --corpus <dir> [--out report.json]
cpipe serve   [--port 4747] [--bind 127.0.0.1]
cpipe model   fetch <model-id> | list | verify
cpipe iqa     run <pipeline.json> <corpus> [--threshold-file thresholds.json]
```

Each subcommand is implemented as a CLI11 `App`, registered in `apps/cli/main.cpp`. The runtime is loaded lazily — `cpipe info nodes` does not initialize Vulkan, for instance.

---

## 11. Pipeline JSON

The Editor produces and consumes a single JSON document per pipeline. Its schema is a subset of the node manifest schema ([Plugin SDK §7](plugin-sdk.md#7-manifest-schema)), so authoring tools and runtime validators are kept in sync.

A minimal, three-node pipeline:

```json
{
  "$schema": "https://schemas.cpipe.dev/pipeline/v0.1.json",
  "version": "0.1",
  "id": "min-dng-to-heif",
  "nodes": [
    { "id": "src",  "type": "com.cpipe.builtin.dng_input",
      "params": {} },
    { "id": "dem",  "type": "com.cpipe.demosaic.amaze",
      "params": { "quality": "high" } },
    { "id": "wb",   "type": "com.cpipe.wb.dual_illuminant",
      "params": {} },
    { "id": "tone", "type": "com.cpipe.tone.filmic_rgb",
      "params": { "ev": 0.0, "contrast": 1.05 } },
    { "id": "out",  "type": "com.cpipe.output.heif_sdr",
      "params": { "quality": 92 } }
  ],
  "edges": [
    { "from": "src.raw",  "to": "dem.in" },
    { "from": "dem.rgb",  "to": "wb.in" },
    { "from": "wb.rgb",   "to": "tone.in" },
    { "from": "tone.rgb", "to": "out.in" }
  ]
}
```

The schema mirrors the manifest's port/cardinality/precision rules. The runtime validates against `pipeline-v0.1.json`; the Editor (Ajv) validates against the same compiled schema served by the runtime at `/api/schemas/pipeline`.

---

## 12. Cross-Cutting Concerns

### Logging

`spdlog` is the runtime logger. The async sink is the default; sinks: console (color) + a rotating file at `~/.cache/cpipe/logs/cpipe.log`. Log levels: `trace / debug / info / warn / error / critical`. The plugin-side `host->log()` ([Plugin SDK §3](plugin-sdk.md#3-c-abi-cpipe_nodeh)) routes into this same logger.

### Tracing

Tracy is the primary in-process profiler. Build flag `CPIPE_ENABLE_TRACY=ON` compiles in the spans; off in Release by default. Spans wrap:

- `Pipeline::run` (whole-run span)
- `Scheduler::dispatch_node` (per-node)
- `ComputeContext::submit_halide / submit_slang / submit_inference`
- `BufferAllocator::create / import_*`

Perfetto SDK exports the same events to a binary trace; Chrome Trace JSON is the editor's consumed format (the editor's profile overlay parses it directly). Tracing is decoupled from logging — high-volume per-node spans never hit the log file.

### Errors

The C ABI returns `cpipe_status_t`; the C++ SDK returns `Result<T> = std::expected<T, Error>`. Plugins **never** throw across the ABI ([Plugin SDK §10](plugin-sdk.md#10-error-handling-result-style)). The runtime classifies errors as:

- **Recoverable** — bad parameter, missing model, unknown action. Logged; the run aborts; the CLI returns non-zero; the editor surfaces a toast.
- **Internal** — null pointer, schema violation, ABI version mismatch. Logged at `critical`; the runtime aborts; on Android a structured crash report is produced.
- **Resource** — OOM, device lost. Logged; the run aborts; the runtime stays up so the editor can reconnect.

The three (logging, tracing, error) are designed to *complement*, not duplicate: an error sets a `Result`, which the runtime logs, while tracing simultaneously closes the active span with the error tag. No single concern carries the others' weight.

---

## 13. Testing Pyramid

Four layers, gated independently in CI:

| Layer | Tooling | Scope | Gate |
|-------|---------|-------|------|
| Unit | Catch2 v3 | core data types, scheduler invariants, manifest validation, JSON schema round-trip | block PR on failure |
| Golden image | OpenImageIO + piq (PSNR / ΔE2000) | per-node EXR fixtures under `tests/golden/<node-id>/` | block PR on failure |
| IQA corpus | piq in-binary + pyiqa subprocess | the 50-image v1 corpus; per-image strict thresholds + corpus-level Wilcoxon signed-rank | block PR on regression beyond threshold |
| Performance | Google Benchmark + nanobench | microbench + corpus run; latency / peak memory recorded to `bench/results/<commit>.json` | recorded only; **no gate** in v1 |

Golden images and corpus files live in Git LFS. Per-node golden tolerances and IQA thresholds live in `tests/thresholds.json`, validated against a schema. Reports are published as a Vega-Lite dashboard on GitHub Pages alongside the editor.

---

## 14. Build & Release

Native:

```
   developer:  cmake --preset linux-debug && cmake --build --preset linux-debug
   CI Linux:   cmake --preset linux-release-clang && ctest --preset ci
   CI Android: cmake --preset android-release-arm64 && ./gradlew :apps:android:assembleRelease
```

Web:

```
   developer:  cd apps/web && npm install && npm run dev
   CI:         npm ci && npm run build && actions/deploy-pages → gh-pages
```

CI matrix runs on every push to `main` or PR:

- Linux x86_64 — Debug + Release; sanitizers (ASAN, UBSAN) on Debug; Tracy off.
- Android arm64 — cross-compile via NDK r27; APK assembled; no instrumentation tests in v1.
- Web Editor — build, type-check, Ajv schema lint; deploy to Pages on `main`.
- Nightly — IQA corpus run; reports published to dashboard branch.

A release tag triggers vcpkg-baseline freeze, version stamp into `cpipe::version()`, and signed binary uploads.

---

## 15. Module × Phase Matrix

The full 12-month roadmap and Definition-of-Done per phase live in [Research 00 §8](research/00-summary.md#8-v1-implementation-roadmap-12-months). This matrix is the orientation for *which module ships when*:

| Module / target              | P0 | P1 | P2 | P3 | P4 | P5 | P6 |
|------------------------------|----|----|----|----|----|----|----|
| `cpipe-core`                 |  ●  |    |    |    |    |    |    |
| `cpipe-runtime` (Linux Vulkan) |  ●  |  ●  |    |    |    |    |    |
| `cpipe-sdk`                  |  ●  |  ●  |    |    |    |    |    |
| `cpipe-builtin-nodes` (5 nodes)  |    |  ●  |    |    |    |    |    |
| `cpipe-builtin-nodes` (18 classic) |    |    |  ●  |    |    |    |    |
| `cpipe-builtin-nodes` (3 AI)  |    |    |    |    |  ●  |    |    |
| Color & HEIF output          |    |  ●  |  ●  |    |    |    |    |
| HDR HEIF + UltraHDR          |    |    |  ●  |    |    |    |    |
| `cpipe-server` + Web Editor  |    |    |    |  ●  |    |    |    |
| IQA harness + perf bench     |    |    |    |  ●  |    |    |    |
| `cpipe-android` + Camera2    |    |    |    |    |    |  ●  |    |
| Hexagon HTP via QAIRT        |    |    |    |    |    |  ●  |    |
| Docs + sample plugin + beta  |    |    |    |    |    |    |  ●  |

(P0 = months 0–1, P1 = 1–3, P2 = 3–5, P3 = 4–7 in parallel with P2, P4 = 5–9, P5 = 7–11, P6 = 11–12.)

---

## 16. v1 Scope Reminders

The architecture intentionally **does not** implement these in v1; reservations are in place so v2 can pick them up without breaking the ABI:

- Tile-based / out-of-core processing ([D2](research/_toc.md#1-decisions-locked-before-research); [Buffer §11](buffer.md#11-sub-view-not-implemented-in-v1)).
- Streaming / live preview pipeline ([D5](research/_toc.md#1-decisions-locked-before-research); [Buffer §8](buffer.md#8-synchronization-host-only)).
- ZSL ring buffer ([D3](research/_toc.md#1-decisions-locked-before-research)).
- Dynamic DAG topology ([D6](research/_toc.md#1-decisions-locked-before-research)).
- External `.so` plugin loading ([D4](research/_toc.md#1-decisions-locked-before-research); [Plugin SDK §5.4](plugin-sdk.md#54-v2-external-so-loading)).
- Plugin sandboxing / marketplace / signing.
- macOS / iOS targets — the Apple platform path under `src/cpipe/runtime/platform/apple/` is stubbed.
- Windows desktop target.
- In-browser node code authoring (Halide / Slang in the editor) — the editor only edits `pipeline.cpipe.json`.
- AI demosaic for Quad Bayer (deferred to v2 with AI demosaic).
- X-Trans demosaic ([D12](research/_toc.md#1-decisions-locked-before-research)).
- Adobe DCP profile *writer* (Q4 — pending license decision).

---

## 17. Open Questions

These come from [Research 00 §9](research/00-summary.md#9-consolidated-open-questions) and remain unresolved at the architecture level. Each is annotated with its impact on the architecture as defined here:

| # | Question | Architecture impact if it flips |
|---|----------|----------------------------------|
| Q1 | Is Adobe DNG SDK 1.7.1 redistributable under Apache 2.0 static linking? | If yes, simplifies `cpipe::ingest::dng` — drops the custom OpcodeList interpreter. |
| Q2 | Does Qualcomm publish a `VK_QCOM_…` extension that lets Vulkan import an HTP buffer? | Eliminates the AHB → HTP memcpy budgeted at the `Handoff` boundary. |
| Q3 | HTP context-binary cache invalidation across OS updates. | Production reliability; no architecture surface change. |
| Q4 | Ship our own DCP writer for v2 calibration profiles, or stay JSON-only? | Adds a `cpipe::color::DcpWriter`; out of scope in v1. |
| Q5 | v2 calibration capture flow — how invasive should the chart-photography UI be? | UX scope; out of scope in v1. |
| Q6 | Quad Bayer DNG GainMap encoding when shooting in 4×4 native mode. | Whether `OpcodeList3` flat-field correction works without custom plumbing. |
| Q7 | Should AI demosaic ship in v1 vs v2 given Quad Bayer quality lift? | Adds an AI demosaic node; differentiation vs schedule trade-off. |
| Q8 | Desktop-only mode for the editor (offline, no network)? | The editor already supports a local-first mode; flipping this only affects defaults. |
| Q9 | Mobile-side Apple Adaptive HDR write fidelity — testing strategy. | Validation; Apple platform is v2. |
| Q10 | Burst frame metadata — single multi-IFD DNG vs N DNGs. | Resolved: **N independent DNGs** for v1. |
| Q11 | Plugin marketplace UI / signing model. | v2 only; no v1 surface. |
| Q12 | Windows v1 build for parity with Linux CLI. | Resolved: **not in v1**; CMake stays portable. |
| Q13 | Multi-camera (logical multi-camera burst). | v2; reserves manifest port `cardinality: "array"`. |
| Q14 | NPU vendor SDK for MediaTek / Samsung — v2 or v3? | v2; isolated to the `DevicePlane`. |
| Q15 | Editor-side authoring of new node types. | Resolved: **not in v1**; editor edits `pipeline.cpipe.json` only. |

---

## 18. See Also

- [`tech.md`](tech.md) — every external dependency, version pin, and license verdict.
- [`buffer.md`](buffer.md) — `IBuffer`, `BufferLayout`, `PixelFormat`, allocator, synchronization, external imports.
- [`plugin-sdk.md`](plugin-sdk.md) — `cpipe_node.h`, manifest schema, lifecycle actions, registration, examples.
- [`research/00-summary.md`](research/00-summary.md) — master research synthesis with the full evidence trail.
- [`research/_toc.md`](research/_toc.md) — locked decisions D1–D19 and out-of-scope list.
