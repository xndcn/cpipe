# Report 06 — Soft-ISP Architectures and Lessons Learned

> **Cluster C — ISP Pipeline & Algorithms.** Architecture-only survey of existing
> open-source and published RAW processors. Drives the cpipe DAG / scheduler /
> node design. **License-aware** per D11: GPLv3 codebases (RawTherapee,
> darktable core) are *architecture inspiration only* — no code reuse.

---

## 1. TL;DR

cpipe must adopt a **vkdt-style heterogeneous compute DAG** with these patterns:
explicit *module layer* (DAG vertices the user/editor sees) over an
*atomic-node layer* (one compute kernel per node, topologically sorted before
dispatch); a single large GPU allocation with offset-bound buffers; pull-based
scheduling against region-of-interest plus precision metadata; parameters
serialized as text and reflected through a `commit_params` callback; one
compute shader per node with explicit input/output channel descriptors. All of
that is from vkdt (BSD-2; safe to study and re-implement). From **darktable**
(GPLv3, study only) we take *scene-referred working space*, *parametric
ordering* (legacy/v3.0/v5.0), the dual `process()` / `process_cl()` IOP
pattern, *pixelpipe variants* (export/preview/thumbnail/HQ), and explicit
upgrade paths via parameter version tags. From **RawTherapee** (GPLv3) the
canonical *linear → Lab → CIECAM* sequence and the demosaic algorithm taxonomy.
From **HDR+** (paper + IPOL re-impl, MIT-style implementation license) the
align/merge/finish three-stage burst structure, tile-based merging, and
underexposed exposure schedule. From **Adobe DNG SDK** (MIT-style) the OpcodeList
and dual-illuminant interpolation reference reader. cpipe v1 = vkdt-style DAG
+ slang-rhi shaders + DNG SDK ingestion + HDR+-derived burst path. (200 words)

---

## 2. Decision Matrix — Architectural Patterns to Adopt

| Pattern                                       | Adopt | Source             | License          | Notes                                                                                  |
|-----------------------------------------------|:----:|:-------------------|:------------------|:---------------------------------------------------------------------------------------|
| Two-layer DAG (module / atomic-node)          | YES  | vkdt               | BSD-2            | Editor binds to module layer; scheduler sees atomic nodes                              |
| Single `vkAllocateMemory` + offset binding    | YES  | vkdt               | BSD-2            | Cuts allocator pressure; matches D2 800 MB FP32 budget                                 |
| One compute shader per atomic node            | YES  | vkdt               | BSD-2            | Push-constants only for iteration counters; uniforms for params                        |
| Topological sort to a linear command buffer   | YES  | vkdt               | BSD-2            | Static topology after load (D6) — canonical schedule baked once                        |
| Parameter file = ASCII tokens + binary form   | YES  | vkdt               | BSD-2            | Editor saves text; runtime can fast-load binary                                        |
| Scene-referred linear working space           | YES  | darktable          | GPLv3 (idea)     | Operate linearly until display transform, do tone mapping last                          |
| Module-order versioning (legacy / v3 / v5)    | YES  | darktable          | GPLv3 (idea)     | Migration path; persisted per-image so old projects stay reproducible                  |
| `process()` + `process_cl()` parity per node  | YES  | darktable          | GPLv3 (idea)     | cpipe maps to `process_cpu()` + `process_gpu()` (slang) + `process_npu()`              |
| Pixelpipe variants (export/preview/HQ/thumb)  | LATER | darktable          | GPLv3 (idea)     | v1 is batch-only (D5); reserve API for v2                                              |
| Linear-Lab-CIECAM ordering                    | PARTIAL | RawTherapee        | GPLv3 (idea)     | Adopt only the linear-first principle; CIECAM is heavy and licensed                    |
| Demosaic algorithm taxonomy                    | PARTIAL | RawTherapee        | GPLv3 (idea)     | We re-implement RCD / AHD / EARI from primary sources, not RT code                     |
| Burst align / merge / finish triple           | YES  | HDR+ (Hasinoff)    | Paper             | IPOL re-impl is MIT-style; safe                                                        |
| Tile-based merging w/ raised-cosine windows   | YES  | HDR+ + IPOL        | Paper / open-src  | Aligns with multi-frame burst-on-shutter (D3)                                          |
| OpcodeList1/2/3 dispatcher                     | YES  | Adobe DNG SDK      | MIT-style         | Reuse the SDK reader; emit our own Halide / slang ops for opcode kernels                |
| Embedded Python preview                        | NO   | darktable          | —                  | Adds runtime weight; v1 is batch CLI                                                    |
| Multi-instance modules                         | YES  | darktable          | GPLv3 (idea)     | Same node type instanced N times in DAG (e.g., dual sharpen)                            |
| Pull-based ROI propagation                     | YES  | darktable, vkdt    | mixed             | Each node declares input ROI as function of output ROI; propagation pre-dispatch        |
| Parameter introspection (manifest)             | YES  | vkdt + custom      | BSD-2 + new       | We add precision flags (D9) to vkdt's connector model                                  |

**Working assumption.** D11 lets us *read* GPLv3 source for ideas; we **must
not** copy-paste, derive close paraphrases of RT / dt code, or link against
their core libraries. We can link `rawspeed` (LGPL2) and `libraw` (LGPL2/CDDL)
because of LGPL static-link compatibility under careful Apache-2 packaging,
but we propose to link **Adobe DNG SDK** (more permissive) as the primary
ingestion path — see [#12 — DNG format](12-dng-format.md).

---

## 3. Detailed Findings

### 3.1 vkdt — the closest spiritual ancestor

**Repo.** `github.com/hanatos/vkdt`, master branch HEAD `967f6460` (2026-05-07).
**Author.** Johannes Hanika, founder of darktable, who started vkdt as a
clean-room Vulkan-only sibling. **License.** 2-clause BSD for original code;
some files inherit other terms (LGPL2 from rawspeed; LGPL2 from FFmpeg
filters). For cpipe, the *original BSD-2 vkdt code* is the one we may study
**and** safely re-implement under Apache-2.

**Graph model (vkdt's `src/pipe/readme.md`).** The system is a "generic node
graph (DAG) which supports multiple inputs and multiple outputs." Two abstraction
layers exist:

1. **Module layer**: read by the configuration parser from `*.cfg`. A module is
   a high-level building block exposed to the user (e.g., `exposure`,
   `colorin`, `denoise`, `o-jpg`). Modules are connected by named connectors.
2. **Atomic-node layer**: invisible to the user. A module emits one or more
   *nodes* — each node is exactly one Vulkan compute shader kernel. The node
   count is a function of (a) the module's logic and (b) the runtime
   region-of-interest (ROI): a denoise module can collapse to fewer nodes when
   given a smaller ROI.

**Memory model.** "We perform one big `vkAllocateMemory` and bind our buffers
to it." Push constants — capped at 128 B / 256 B by Vulkan spec — are used
**only for iteration counters**, never for parameter blobs. Module parameters
go through uniform buffers; the parameter interface supports strings, with a
`commit_params` callback for custom text-to-float translation (e.g., LensFun
name → polynomial coefficients).

**Scheduling.** The DAG is *flattened to a linear pipeline by topological sort*
right before dispatch. `graph.h` performs (i) topological sort, (ii) buffer
lifetime analysis to allocate from the single arena, (iii) tile-loop
generation if needed (vkdt has tiling; cpipe defers per D2). One Vulkan command
buffer is recorded per pipeline run.

**File format.** The pipeline graph is an ASCII config file with line tokens
of the form `module:input:port:value` or `connect:m1:o:m2:i`. A binary form
is supported for fast loading. Both formats survive editor round-trip.

**What cpipe takes.** Everything in the layering, single allocation, one-shader-per-node
mapping, push-constant discipline, and ASCII config. We change four things:

* swap GLSL / Vulkan for **slang** + slang-rhi (D15 starting candidate)
  so the same shader compiles to Vulkan/Metal/D3D12 (and slang's WebGPU work
  can target the editor preview later);
* add a per-node **precision manifest** (D9) so the scheduler can insert FP16↔FP32
  conversion ops minimally;
* add a `process_npu()` invocation channel that routes nodes whose manifest
  declares NPU support (D13 — Hexagon / ANE) to the inference layer ([#04](04-mobile-ai-inference.md), [#05](05-npu-backends-zero-copy.md));
* add a `serialize` API for the editor (React Flow) so the same graph file
  can be sent over WebSocket (in [#11](11-pipeline-editor-and-connectivity.md)).

**Threading (gap in vkdt's documentation).** vkdt explicitly relies on Vulkan
for all parallelism — there is no host-side `pthread` work queue inside the
pipeline beyond the main thread that records the command buffer. That works
for D2 (single 100 MP image at a time, batch only). For multi-image batches,
cpipe will use **TaskFlow** ([#03](03-heterogeneous-scheduler.md)) to overlap
*per-image* graphs.

### 3.2 darktable — the canonical FOSS reference (study only, GPLv3)

**Repo.** `github.com/darktable-org/darktable`, master HEAD `e0b02945` (2026-05-08).
**Pixelpipe.** `dt_dev_pixelpipe_t` executes a *linear* sequence of IOP
modules — not literally a DAG with branches in v1 — but each node has rich
metadata about ROI propagation, scale, and processing order, so the
implementation behaves like a topologically-sorted DAG with a single chain.

**IOP module structure.** Each module exports:

* `process()` — pure C path, used for CPU fallback and floats.
* `process_cl()` — OpenCL path. Modules acquire an exclusive device
  through `dt_opencl_lock_device()` for the duration of a pipeline run.
* `commit_params()` — translate UI parameters into the kernel-friendly form.
* `init_pipe()`/`cleanup_pipe()` — per-pipeline state.
* `distort_transform()`/`distort_backtransform()` — ROI propagation for
  geometry modules (lens / crop / perspective).
* `legacy_params()` — migrate parameters when the on-disk version differs
  from the running version. **This is the single most important pattern darktable
  has and most other RAW pipelines lack.** Without it, history stacks become
  un-replayable across software upgrades.

**Module-order schemes.** darktable maintains *multiple* canonical orderings,
versioned: `legacy` (pre-3.0, display-referred), `v3.0`, `v5.0`
(scene-referred default in 2025 / 2026 builds). Users can pick custom orders
via `Ctrl+Shift+drag`. The ordering is persisted **per image** so older edits
remain reproducible after a default change. The DeepWiki page
"Module Order and IOP Ordering" makes this explicit.

**Pixelpipe variants.** Four variants with different cost/quality knobs:

* **Export pipe** — full-res, max quality, slowest (used for save).
* **Preview pipe** — full-image, low-res, used as context for darkroom.
* **Darkroom pipe** — cropped to viewport ROI, optimised for responsiveness.
* **Thumbnail pipe** — tiny, fast, used in lighttable.
* **High-quality processing mode** — toggles slow algorithms ON for export.

For cpipe v1 we have *only* the export pipe (D5: batch-only). The architecture
must keep the door open for a preview pipe later — concretely: ROI propagation
must already be in place even though v1 always runs ROI = full image.

**Scene- vs display-referred.** Default since darktable 3.6 / current 5.x is
scene-referred: keep working space linear and unbounded (HDR-shaped) until
filmic/sigmoid maps to display range as the *last* step before output
transform. cpipe inherits this (D7 needs scene-referred working space to
generate UltraHDR / HDR HEIF outputs without re-grading).

**OpenCL acceleration patterns.** The OpenCL subsystem in darktable
(`src/common/opencl.[ch]`) is the closest thing to a battle-tested compute
abstraction in the FOSS RAW world: device priority, multi-device round-robin,
per-pixelpipe locking, kernel cache, and a fallback to `process()` if any
kernel fails. We do not copy this code — we re-architect it as
`compute::Device` in cpipe over slang-rhi ([#01](01-compute-frameworks.md)).

**Parameter introspection.** darktable serializes parameters as a single
versioned C struct, blob-stored in the per-image XMP / sqlite history. cpipe
prefers a more editor-friendly form: a manifest declaring each parameter's
type, range, default, and version, generated from the node's C++ source
(macros similar to DPF / VST3) so the React Flow editor can render UI
without bespoke per-node code.

### 3.3 RawTherapee + ART — GPLv3, value is in the algorithm taxonomy

**Repo.** `github.com/RawTherapee/RawTherapee`. **License.** GPLv3.

**Pipeline ordering ("Toolchain Pipeline" page on RawPedia).** The canonical
RT order, top-to-bottom:

1. **Preprocess** in linear sensor space — dark frame, flat field, hot-pixel,
   black point, line-noise, chromatic aberration auto, white point.
2. **Demosaic** — RCD / AMaZE / DCB / VNG4 / IGV / LMMSE / EAHD / PPG / fast.
3. **Highlight reconstruction** — color propagation, blend, luminance recovery.
4. **White balance** — temperature/tint or auto.
5. **Spot removal**, **crop**.
6. **Conversion to working profile** (linear ProPhoto / Rec.2020 / sRGB).
7. **RGB-domain** processing — exposure compensation, channel mixer, HSV,
   tone curve, B&W, film simulation.
8. **L\*a\*b\* domain** — shadows/highlights, local contrast, sharpening,
   wavelets, vibrance.
9. **CIECAM02 / CIECAM16** for perceptual colorimetric correction.
10. **Output transform** to chosen ICC profile.

ART (Another RawTherapee) is a fork that simplifies the UI and adds local
edits with masks, ACES CLF / CTL support, exiv2 metadata, and optional
LibRaw raw decoding. Both are still GPLv3. We **don't** ship CIECAM in v1 —
it's heavy and the canonical implementations are GPLv3 (CIECAM02 specifically:
`rtengine/ciecam02.cc`). For perceptual tone we'll use the OCIO ACES path
([#13](13-color-management.md)).

**What cpipe takes.** The *order itself* is canonical (linear → working-space
RGB → Lab → CIECAM → output). We adopt it minus the CIECAM step. The RT
demosaic naming is itself a useful taxonomy ([#07](07-classic-isp-algorithms.md)).

### 3.4 Adobe Camera Raw / Lightroom — closed but with a public reference

**ACR / Lightroom** are closed-source, but Adobe ships the **DNG SDK** as a
permissive, royalty-free **reference processor**: read DNG, apply black/white
levels, evaluate `OpcodeList1`, demosaic, evaluate `OpcodeList2`, white
balance via interpolated `ColorMatrix1/2`, color transform via interpolated
`ForwardMatrix1/2`, evaluate `OpcodeList3`, and emit linear ProPhoto. The DNG
SDK license (per `scancode-licensedb.aboutcode.org`) grants a "non-exclusive,
worldwide, royalty free license to use, reproduce, prepare derivative works
from, publicly display, publicly perform, distribute and sublicense the
Software for any purpose" — an MIT-style grant, fully Apache-2 compatible.

A meson-build modernisation lives at `github.com/hfiguiere/dng_sdk` (verified
2026-05). **cpipe will link the DNG SDK** for ingestion. The SDK is not a
*pipeline*; it's a metadata-aware loader. We map its `dng_negative` →
cpipe's `RawImage` once, then run our own DAG.

Public materials on Lightroom / ACR architecture come from CVPR / SIGGRAPH
talks and Adobe blog posts; no specific library reuse is possible. Lessons
to take: (1) every parameter must round-trip non-destructively, (2) profile
+ preset systems are first-class, (3) tone curves must be authored as
single-channel and applied in scene-referred linear before display transform.

### 3.5 LibRaw — LGPL2/CDDL — input only

**Repo.** `github.com/LibRaw/LibRaw`. **License.** LGPL 2.1 or CDDL 1.0
(user choice). **Architecture.** Thin C/C++ API over Dave Coffin's `dcraw`,
plus extra demosaic algorithms in a separate `LibRaw-demosaic-pack`. Provides:
parsed metadata, raw pixel array, embedded preview/thumbnail, simple
post-processing (white balance, gamma) inherited from dcraw.

**Status for cpipe.** LGPL2 is *Apache-2 link-compatible* under static
linking with the standard caveats (must allow user replacement). We can use
LibRaw as a fallback ingestion path for cameras whose proprietary RAW the
DNG SDK does not parse. **Primary ingestion path is the DNG SDK** because
D10 (calibration via DNG metadata) means we *expect* DNG input.

### 3.6 Halide-based ISPs and academic prior art

**Halide.** Embedded DSL in C++ that decouples *algorithm* (what) from
*schedule* (how) — separation that vkdt and darktable do not have, and that
makes Halide attractive as a *node implementation language* in cpipe. The
canonical paper is Ragan-Kelley et al. PLDI 2013; Halide is now used in
Google products (HDR+, auto-enhance) and ships with multiple camera-pipeline
demos (`apps/camera_pipe`).

**Notable open-source Halide ISPs.**

* **`halide_camera_pipeline`** — small reference camera pipe in Halide, MIT.
* **`darkroom`** — Stanford research, image processing on FPGAs.
* **`HDRplus` (open-source IPOL re-implementation, 2021)** — Hasinoff et al.'s
  burst pipeline, re-built end-to-end in Halide; published with full source
  in IPOL (open access, attribution licence). This is the only open-source
  end-to-end HDR+ implementation worth studying. Authors Monod, Facciolo,
  Morel; arXiv 2110.09354. Repository at
  `github.com/timothybrooks/hdr-plus`.

**Status for cpipe.** Halide is a strong candidate as a *node implementation
language* in v1 for CPU-side and Vulkan/Metal targets, complementing slang
shaders for GPU-only nodes. Decision deferred to [#01 — Compute frameworks](01-compute-frameworks.md).

### 3.7 Google HDR+ pipeline (the gold standard for burst)

**Paper.** Hasinoff et al., "Burst photography for high dynamic range and
low-light imaging on mobile cameras", SIGGRAPH Asia 2016
(`research.google/pubs/burst-photography-...`). **Companion paper.** Wronski
et al., "Handheld Multi-Frame Super-Resolution", SIGGRAPH 2019
(`arxiv.org/abs/1905.03277`).

**Architecture (3 stages).** This is the template every modern mobile burst
pipeline copies, including Apple Smart HDR.

1. **Align.** Hierarchical Gaussian-pyramid alignment from coarse to fine.
   16 × 16 tiles with a 4-pixel search range at the coarsest level. L1 distance
   chosen for speed and robustness to shot noise. Output = per-tile motion
   vector field, stored at one granularity per pyramid level.
2. **Merge.** Frequency-domain or patch-similarity merging on Bayer raw.
   Tiles overlap 50 % with a *raised-cosine* window for blending. Production
   HDR+ uses Wiener-filter merging in the frequency domain; the IPOL
   re-implementation uses non-local means-style patch similarity.
   *Critical*: merging happens on **Bayer**, not RGB. This preserves accuracy
   and matches the simpler noise model.
3. **Finish.** Black-level subtraction, white balance from DNG metadata,
   demosaic with simplified gradient correction, bilinear chroma denoise,
   sRGB color correction matrix, tone mapping (Laplacian-pyramid
   exposure-fusion-style local tone mapping), gamma, global contrast,
   unsharp mask.

**Performance.** Eight raw frames in **3.4 s** on quad-core CPU per IPOL
benchmark. Production HDR+ on Pixel takes ~1 s thanks to GPU paths
(Halide-scheduled for ARM Mali / Adreno).

**Burst exposure schedule.** Underexpose every frame deliberately — there's
always headroom — then merge to recover SNR. This is *not* exposure
bracketing; all frames have the same (under)exposure. In ZSL mode, the camera
maintains a ring buffer of underexposed shots and uses the moments before
shutter press. **cpipe defers ZSL** (D3) but keeps the same merge math: 5–10
frames, all shutter-or-after.

**What cpipe takes.** The whole 3-stage structure, pyramid alignment, 50 %
overlap with raised-cosine window, Bayer-domain merging, Laplacian-pyramid
local tone mapping, optional Wronski super-resolution as a *node* (not
required v1).

### 3.8 Other open mobile ISPs

* **Raspberry Pi `libcamera` ISP** — kernel + user-space pipeline for the
  Pi camera. Tunable JSON config, focused on *streaming* (real-time camera
  preview). Architecture lessons: tunable file format, statistics → control
  loop, but its scheduler is built around a single hardware ISP block we
  don't have. Not directly applicable.
* **Rockchip `rkisp1`** — SoC vendor ISP exposed via V4L2; closed user-space
  control loops. Not useful as a code reference but useful as an end-to-end
  example of how a metadata-driven hardware ISP is wrapped.
* **PiCamera2 / picamera2 raw** — Python; useful as a *consumer of* a soft
  ISP but not a soft ISP itself.

### 3.9 Closed mobile ISPs (architecture lessons only)

**Apple's smart HDR / Deep Fusion.** CVPR 2024 paper "Adaptive HDR Apple"
described the gain map / SDR base / metadata format. Architecturally we
absorb three lessons: (1) gain-map HDR is the right v1 output (D7); (2)
the SDR base must remain *correct on its own* — never rely on a HDR-only
viewer; (3) per-frame metadata must travel through HEIF.

**Capture One.** Closed; nothing reusable. Architecture watch points: their
"sessions" model is a pure project-organisation pattern; out of v1 scope.

**AfterShot Pro / DxO PhotoLab.** Closed. DxO's lens module library is
gold; we cannot license but DNG OpcodeList3 covers a lot of the same ground.

### 3.10 Multi-instance modules — a darktable insight worth taking

darktable lets the user place the *same module type* multiple times in the
pipeline (e.g., two `sharpen` nodes — one before tone, one after). The IOP
order machinery handles this by tagging each instance with a UUID and
storing per-instance parameters. cpipe inherits this verbatim: a node in the
DAG is identified by `(node_type, instance_uuid)`, with the editor allowed
to add multiple instances of the same type.

### 3.11 Parameter introspection — what every editor needs

vkdt's connectors carry just channel count + element type. darktable's
parameter introspection is via per-module C struct + `dt_iop_params_t`. The
React Flow editor cpipe targets needs richer data: parameter type
(`float|int|bool|enum|color|curve|lut|file`), range, default, label, group
("Basic"/"Advanced"), tooltip, plus precision (D9). We propose a
declarative manifest emitted at compile-time by macros over the node's C++
struct, plus a runtime introspection API for the editor:

```cpp
struct NodeManifest {
  std::string  type_id;        // e.g., "demosaic.rcd"
  std::string  display_name;
  std::vector<Connector> inputs;
  std::vector<Connector> outputs;
  std::vector<Param>     params;
  PrecisionPolicy        precision;   // see D9
  DeviceMask             devices;     // CPU | GPU | NPU
  uint32_t               version;     // for legacy_params() migration
};
```

### 3.12 Pipeline serialization formats

Three options surveyed:

| Format          | Size    | Editor-friendly | Backwards-compat | Who uses it     |
|------------------|---------|------------------|--------------------|-------------------|
| ASCII tokens     | small   | medium           | by version field   | vkdt, RT (PP3)    |
| JSON (custom)    | medium  | high             | by schema version  | OpenFX, ComfyUI   |
| Binary protobuf  | small   | low              | strong typing      | TF SavedModel     |

**Recommendation.** **JSON** for v1 — the editor is React Flow, JSON is the
native serialization there; humans can diff. We additionally cache a binary
form for fast load. JSON schema for the graph is finalized in
[#10 — Plugin architecture](10-plugin-architecture.md).

---

## 4. Recommended cpipe Pipeline Architecture

### 4.1 Two-layer DAG — formalised

Borrow vkdt's separation literally:

```
[Project JSON] ─► [Module DAG]  (user-facing nodes + connections)
                    │
                    ▼
              [Atomic-Node DAG] (one compute kernel per node)
                    │   topo-sort
                    ▼
            [Linear schedule + memory plan]
                    │
                    ▼
       [Slang-rhi command buffer / TaskFlow CPU graph]
```

A *module* is what the user / editor manipulates. A *node* is what the
scheduler / GPU sees. A module emits 1..N nodes at *build* time, parameterized
by the runtime ROI and precision policy.

### 4.2 Static topology (D6)

After load, the topology — both layers — is frozen. *Parameters* still mutate;
node count, connectors, precision, scheduling do not. This makes the linear
command buffer reusable across re-runs of the same project (different
parameters but same structure).

### 4.3 Memory plan

A single arena per device. cpipe runs the equivalent of a scratchpad analysis:
(1) compute per-buffer first-use / last-use; (2) coalesce same-shape
buffers whose lifetimes are disjoint; (3) emit a flat allocation. With the
D2 800 MB worst-case FP32 figure as the hard ceiling, the planner must reject
graphs that exceed it (until tile-based comes in v2).

### 4.4 Node scheduling

* **CPU nodes**: TaskFlow tasks. Fan-out by image (batch). One node = one
  task; node-internal tile parallelism is the kernel's responsibility.
* **GPU nodes**: slang-rhi command buffer. Topological order; dependencies
  encoded as Vulkan timeline semaphores / Metal events.
* **NPU nodes**: routed to the inference engine ([#04](04-mobile-ai-inference.md));
  inputs and outputs are zero-copy buffers ([#02](02-zero-copy-buffer-architecture.md)).
* **Hybrid nodes** (e.g., classic pre-processing then NN): expressed as two
  separate atomic nodes in the DAG, never as one polymorphic node.

### 4.5 Parameter propagation

* Editor edits → JSON patch → loaded into `Project`.
* `Project::commit()` invokes `commit_params()` on each affected node.
* `commit_params` translates UI values (e.g., temperature 5500 K) into
  kernel-friendly form (e.g., RGB multipliers).
* Precomputed kernel inputs go into the node's uniform buffer block.
* No structural change → no re-sort of the schedule. Just re-record the
  command buffer if any ROI / precision changed.

### 4.6 Persistence

* `project.json` is the authoritative form: graph topology + node parameters
  + working ICC + viewer profile.
* `project.cache.bin` is an optional binary mirror keyed by JSON hash for
  fast reload.
* Per-image XMP sidecar carries DNG path + project pointer (mirrors
  darktable's pattern for portability).

### 4.7 Burst-on-shutter (D3) handling

Bursts arrive as N RAW frames. The first ingestion node consumes the burst
buffer; subsequent nodes operate on the merged Bayer (after `align_merge`).
Merging is a single atomic node: input N Bayer raws + reference index;
output one Bayer raw.

```
[InputBurst N×Bayer] → [Align (HDR+)] → [Merge (HDR+)] → [Finish chain ...]
```

### 4.8 Precision plan (D9 plumbing)

Each manifest declares `input_precision` and `output_precision` per
connector. The schedule optimiser inserts the **minimum** number of FP16↔FP32
conversion ops, by:

1. Walking the DAG in topo order.
2. For each connector, intersecting required precisions; if mismatch,
   schedule a `convert` micro-node before consuming.
3. For consecutive `convert(FP32)→f→convert(FP16)→g→convert(FP32)`, fuse
   into `convert→f→g→convert` if `f`+`g` both run in FP32.

### 4.9 Failure modes the v1 architecture must handle

* **GPU OOM**: detect at memory plan time, refuse to run (D2 budget).
* **Driver crash**: surface to caller; do not auto-fall-back to CPU yet
  (deferred to v2 — adds complexity).
* **Bad parameter from editor**: schema-validated at JSON load; the
  parameter is rejected before `commit_params()` runs.

---

## 5. Code / Architecture Sketches

### 5.1 Node manifest macro

```cpp
CPIPE_NODE("demosaic.rcd", "Demosaic — RCD", 1)
    CPIPE_INPUT_BAYER("in", 16)        // FP16
    CPIPE_OUTPUT_RGB("out", 16)
    CPIPE_PARAM_FLOAT("threshold", 0.0f, 1.0f, 0.5f)
    CPIPE_PARAM_BOOL ("use_eari",  false)
    CPIPE_DEVICES(GPU | CPU)           // not NPU
CPIPE_NODE_END

class DemosaicRCD : public NodeImpl {
public:
    void process_gpu(SlangDispatcher&, const Roi&, const Params&) override;
    void process_cpu(const Roi&, const Params&) override;
    Roi  roi_in_for(const Roi& roi_out) const override { return roi_out; }
};
```

The macro builds the static `NodeManifest`, registers it in
`NodeRegistry`, and produces the `legacy_params()` shim if `version` is bumped.

### 5.2 Pipeline JSON (illustrative)

```json
{
  "version": 1,
  "nodes": [
    {"id":"in.0","type":"input.dng","params":{"path":"shot.dng"}},
    {"id":"merge.0","type":"burst.align_merge","params":{"frames":7}},
    {"id":"black.0","type":"raw.black_white"},
    {"id":"opc2.0","type":"dng.opcodelist2"},
    {"id":"demo.0","type":"demosaic.rcd","params":{"use_eari":true}},
    {"id":"opc3.0","type":"dng.opcodelist3"},
    {"id":"wb.0","type":"color.wb_dng_dual"},
    {"id":"cmat.0","type":"color.forward_matrix_dual"},
    {"id":"oce.0","type":"color.icc_in"},
    {"id":"den.0","type":"denoise.bm3d"},
    {"id":"shr.0","type":"sharpen.usm"},
    {"id":"tone.0","type":"tone.filmic_rgb"},
    {"id":"oco.0","type":"color.icc_out"},
    {"id":"out.0","type":"output.heif"}
  ],
  "edges": [
    ["in.0:out","merge.0:in"],
    ["merge.0:out","black.0:in"],
    ["black.0:out","opc2.0:in"],
    ["opc2.0:out","demo.0:in"],
    ["demo.0:out","opc3.0:in"],
    ["opc3.0:out","wb.0:in"],
    ["wb.0:out","cmat.0:in"],
    ["cmat.0:out","oce.0:in"],
    ["oce.0:out","den.0:in"],
    ["den.0:out","shr.0:in"],
    ["shr.0:out","tone.0:in"],
    ["tone.0:out","oco.0:in"],
    ["oco.0:out","out.0:in"]
  ]
}
```

### 5.3 Scheduler skeleton

```cpp
class Scheduler {
public:
    Schedule build(const Graph& g);
    void     run  (const Schedule&, ProjectParams&);
private:
    MemoryPlan plan_memory(const Graph& g);
    Buffer     allocate_arena(const MemoryPlan&, Device&);
    void       record_command_buffer(const Schedule&, ProjectParams&);
};
```

`Schedule` holds: ordered list of nodes, per-node device assignment, per-buffer
arena offsets, FP precision conversion ops, and zero-copy hand-off points to
NPU.

### 5.4 ROI propagation

```cpp
struct Roi { int x, y, w, h; int scale; };

// Each node implements:
Roi roi_in_for(const Roi& roi_out) const;

// Scheduler walks backwards from sinks to sources:
for (Node* n : reverse_topo(graph)) {
    n->set_roi_in(n->roi_in_for(n->roi_out()));
}
```

Implements darktable's ROI model. v1 sinks request full image always (D5
no preview), but the machinery works for v2.

---

## 6. Cited Sources

* vkdt — `github.com/hanatos/vkdt` (HEAD `967f6460`, 2026-05-07). License:
  2-clause BSD with bundled LGPL2 modules.
  Pipeline readme:
  `github.com/hanatos/vkdt/blob/master/src/pipe/readme.md`.
  Project page: `jo.dreggn.org/vkdt/readme.html`.
* darktable — `github.com/darktable-org/darktable` (HEAD `e0b02945`, 2026-05-08).
  License: GPLv3.
  User-manual pixelpipe: `docs.darktable.org/usermanual/development/en/darkroom/pixelpipe/`.
  Module-order doc: `docs.darktable.org/usermanual/development/en/darkroom/pixelpipe/the-pixelpipe-and-module-order/`.
  DeepWiki: `deepwiki.com/darktable-org/darktable`.
* RawTherapee — `github.com/RawTherapee/RawTherapee`. License: GPLv3.
  Toolchain pipeline: `rawpedia.rawtherapee.com/Toolchain_Pipeline`.
  Demosaicing taxonomy: `rawpedia.rawtherapee.com/Demosaicing`.
* ART (Another RawTherapee) — `github.com/artraweditor/ART`. License: GPLv3.
  Project page: `artraweditor.github.io`.
* Adobe DNG SDK — DNG SDK 1.7 (released 2025).
  License: `scancode-licensedb.aboutcode.org/adobe-dng-sdk.html` (MIT-style).
  Modernised mirror: `github.com/hfiguiere/dng_sdk`.
  Spec PDF: `paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf`.
* LibRaw — `github.com/LibRaw/LibRaw`. License: LGPL 2.1 / CDDL 1.0
  (`www.libraw.org/node/2228`). About: `www.libraw.org/about`.
* Halide — `halide-lang.org`. Ragan-Kelley et al. PLDI 2013 (DOI
  `10.1145/2491956.2462176`); CACM 61(1) revision (DOI `10.1145/3150211`).
* HDR+ — Hasinoff et al., "Burst photography for high dynamic range and
  low-light imaging on mobile cameras", SIGGRAPH Asia 2016
  (`research.google/pubs/burst-photography-for-high-dynamic-range-and-low-light-imaging-on-mobile-cameras/`).
* Wronski — Wronski et al., "Handheld Multi-Frame Super-Resolution",
  SIGGRAPH 2019 (`arxiv.org/abs/1905.03277`).
* IPOL HDR+ re-implementation — Monod, Facciolo, Morel,
  arXiv 2110.09354, IPOL 2021 (`www.ipol.im/pub/art/2021/336/`).
* Tim Brooks HDR+ Halide port — `www.timothybrooks.com/tech/hdr-plus/`.
  Repo: `github.com/timothybrooks/hdr-plus`.
* Filmic RGB — Pierre, "Filmic, darktable and the quest of the HDR tone
  mapping" (`eng.aurelienpierre.com/2018/11/filmic-darktable-and-the-quest-of-the-hdr-tone-mapping/`),
  manual `docs.darktable.org/usermanual/4.8/en/module-reference/processing-modules/filmic-rgb/`.
* HDR+ dataset — `hdrplusdata.org/dataset.html`.

---

## 7. See also

* [#01 — Compute frameworks](01-compute-frameworks.md) — slang-rhi vs Halide
  decision; impacts whether nodes ship as slang shaders or Halide JIT.
* [#02 — Zero-copy buffer architecture](02-zero-copy-buffer-architecture.md) —
  zero-copy GPU↔NPU hand-off for hybrid nodes.
* [#03 — Heterogeneous scheduler](03-heterogeneous-scheduler.md) — TaskFlow
  integration with the per-image and per-node scheduling.
* [#04 — Mobile AI inference](04-mobile-ai-inference.md) — engine for AI
  nodes called from the DAG.
* [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — node-by-node
  algorithm choices riding on this architecture.
* [#08 — AI ISP algorithms](08-ai-isp-algorithms.md) — neural nodes the
  architecture must accommodate.
* [#10 — Plugin architecture](10-plugin-architecture.md) — node SDK and ABI.
* [#11 — Pipeline editor](11-pipeline-editor-and-connectivity.md) — React
  Flow ↔ DAG round-trip.
* [#12 — DNG format](12-dng-format.md) — input pipeline.
* [#13 — Color management](13-color-management.md) — working space and
  output transform.
* [#14 — HEIF and HDR output](14-heif-and-hdr-output.md) — sink nodes.
* [#15 — Mobile camera calibration](15-mobile-camera-calibration.md) —
  what `OpcodeList1/2/3` produce as inputs.

---

## 8. Open Questions

1. **Halide vs slang for node implementation language.** vkdt commits to one
   shader language (GLSL → SPIR-V). cpipe's slang-rhi-first stance handles
   most but not all targets. Should we permit a *Halide pipeline* as a node
   body, treating Halide JIT output as opaque CPU/GPU code? This question
   carries into [#01](01-compute-frameworks.md).
2. **Multi-image batch parallelism via TaskFlow vs dispatch streams.**
   Concurrency on a single GPU between two same-graph runs of different
   images is not free — they would contend for the arena. Resolution: per-image
   command-buffer arena (cheap), but doubles VRAM peak.
3. **Pixelpipe variants (preview / thumb / hq) reservation.** v1 is batch-only.
   The architecture leaves the door open via per-pipeline ROI, but should we
   keep variant-aware kernel dispatch in v1 even if the only variant is
   "export"? Cost: small. Benefit: avoids a refactor in v2.
4. **CIECAM02 / 16 in scope?** Not v1. But the OCIO ACES path covers most
   perceptual needs; defer.
5. **Per-image sidecar format.** XMP (darktable, Lightroom) vs JSON sidecar
   vs SQLite? cpipe leans JSON sidecar (D-side), but XMP travel preserves
   round-trip with Lightroom. Decision deferred to [#10](10-plugin-architecture.md).
6. **Multi-instance node naming.** UUID vs name + counter (e.g., `sharpen.0`,
   `sharpen.1`)? UUID is robust; name+counter is human-readable. We propose
   stable UUID + display alias.
7. **darktable's `legacy_params()` migration: how aggressive?** v1 won't
   ship many versions, but the *infrastructure* must be in place from v0 to
   avoid catastrophic schema lock-in later.
8. **Slang shader source distribution.** Ship as compiled SPIR-V (binary,
   one-shot), or compile at runtime from source? Runtime compilation enables
   the editor to hot-reload nodes. v1: ship pre-compiled; v2 hot-reload.
