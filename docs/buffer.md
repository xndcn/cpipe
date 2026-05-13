# cpipe Buffer Architecture

> Date: 2026-05-08 · Companion: [`plugin-sdk.md`](plugin-sdk.md) · Research: [`research/02-zero-copy-buffer-architecture.md`](research/02-zero-copy-buffer-architecture.md)

The Buffer subsystem is cpipe's data substrate. This document defines the `cpipe::compute::IBuffer` abstraction, the `PixelFormat` / `BufferLayout` data types, the Vulkan / Metal / AHardwareBuffer / IOSurface backend bindings, the contract between external producers (Camera2 / DngReader) and the pipeline, CPU synchronization primitives, and v1 scope limits. **The plugin C ABI is not defined here** — see [`plugin-sdk.md`](plugin-sdk.md).

---

## 1. Decision Summary

| # | Decision | Note |
|---|----------|------|
| B1 | `IBuffer` carries 4 logical resource kinds: 2D image / 3D volume (3D LUT) / NCHW Tensor / BLOB | Precision is encoded in `PixelFormat`; no separate `ITensor` abstraction |
| B2 | Burst (5–10 RAW) is N independent `shared_ptr<IBuffer>`; nodes declare `cardinality:"array"` ports | No `BatchedBuffer` wrapper |
| B3 | No multi-plane (YCbCr) support | `BufferLayout` has no `plane_count` field |
| B4 | Synchronization (`IFence`, `ITimeline`) is strictly host-only; invisible to plugins | The scheduler waits on input edge timeline values before `process()` and signals output edge timeline values after |
| B5 | Native handles (VkBuffer / AHardwareBuffer / MTLTexture) are **not** exposed across the C ABI; plugins submit work through `ComputeContext` only | Host-side C++ may still unpack natives — see §7 |
| B6 | CPU access goes through `lock_cpu / unlock_cpu / flush_cpu_writes` in the C ABI buffer suite | Only CPU-device nodes (libheif encoder, libraw reader) call these |
| B7 | `IBuffer::sub_view` reserved as a v1 signature only; not implemented | Tile-based processing is a v2 topic |
| B8 | Internal allocator = VMA + MTLHeap; external imports (AHB / IOSurface / host pointer) go through VMA's import path | API is per-call; backend is sub-pool |
| B9 | DngReader / Camera2BufferProducer are decoupled from `Pipeline`; not plugin nodes | They produce `IBuffer` as host classes that feed pipeline input ports |
| B10 | Plugins see `cpipe::sdk::Buffer` backed by `shared_ptr<IBuffer>` | Native ownership is shared across backends (e.g. Vulkan + NPU on the same AHB) |
| B11 | OCIO role lives in `BufferMetadata::cs_role`; only dedicated color nodes mutate it | Color transforms still happen in dedicated nodes; the role string is now writable through the metadata builder, not a free-standing IBuffer setter |
| B12 | v1 scope limits: same-process only; no tile-based / streaming / hot-reload / sandboxing | Architecture does not block v2; v1 simply does not implement these |
| B13 | Every `IBuffer` carries `shared_ptr<const BufferMetadata>`; metadata is an immutable snapshot, derived per output | `RunInputs::meta` is removed; metadata travels on the buffers themselves so burst frames carry per-frame metadata naturally |
| B14 | `BufferMetadata` covers six categories: capture, calibration, processing-state, color, output-sidecar, tensor-quant | Stable typed accessors for the v1-frozen DNG/Camera2/HEIF fields; vendor / EXIF / XMP / opcode-bytes go through reverse-DNS keyed blobs |
| B15 | Burst calibration sub-blocks are shared via an internal `shared_ptr<const CalibrationBlock>`; the public surface stays single-layer | Plugins do not see two scopes; ingest constructs N frame metadata records that share one calibration pointer |
| B16 | Scratch buffers (returned by `ComputeContext::request_scratch`) carry `metadata = nullptr` | Plugins must not call metadata getters on scratch; they live for one `process()` call and never enter the DAG |

---

## 2. Architecture Overview

```
                      +----------------------------------------------+
                      |  Producer (host-only)                        |
   DNG file  --> DngReader   -----------+                            |
                                        |                            |
   Camera2 burst --> Camera2Buffer-     | shared_ptr<IBuffer>        |
                     Producer ----------+                            |
                                        v                            |
                      +----------------------------------------------+
                      |  Pipeline (DAG)                              |
                      |                                              |
                      |  Node --edge--> Node --edge--> Node          |
                      |   |              |              |            |
                      |   v              v              v            |
                      |  ComputeContext (host-mediated)              |
                      |   |              |              |            |
                      |   v              v              v            |
                      |  Halide AOT | Slang/RHI | ExecuTorch/QNN/CoreML |
                      |   |              |              |            |
                      |   v              v              v            |
                      | +-- Allocator -------------------------+     |
                      | | Vulkan: VMA   Metal: MTLHeap   CPU: aligned | |
                      | +--------------------------------------------+ |
                      |                                              |
                      |  IFence / ITimeline (host-only)              |
                      |  owned by scheduler                          |
                      +----------------------------------------------+
```

- **Producer** (DngReader / Camera2BufferProducer) — host classes, not plugin nodes. They produce `shared_ptr<IBuffer>` (each carrying a `shared_ptr<const BufferMetadata>`) to feed pipeline input ports before `pipeline.run(inputs)` is invoked.
- **Pipeline** — a DAG of node instances; each node is a plugin (see [`plugin-sdk.md`](plugin-sdk.md)). Edges carry `shared_ptr<IBuffer>`; metadata travels with the pixel data — see §6.
- **ComputeContext** — the facade host gives plugins. A plugin only submits Halide / Slang / Inference work through `ComputeContext`; native handles, fences, timelines and command queues are invisible to it.
- **Allocator** — Vulkan uses VMA, Metal uses an `MTLHeap` pool, CPU uses aligned `posix_memalign`. External imports (AHB / IOSurface / host pointer) also enter via VMA's import path.
- **Scheduler** — owns timelines and fences; before each `process()`, it waits for all input edges to reach their timeline values; after, it signals output edge timeline values. Plugins never see this.

Detailed producer / consumer chains (Linux desktop / Android / Apple v2) are in [`research/02-zero-copy-buffer-architecture.md §6`](research/02-zero-copy-buffer-architecture.md).

---

## 3. PixelFormat

```cpp
namespace cpipe::compute {

enum class PixelFormat : uint16_t {
    UNDEFINED = 0,

    // RAW / Bayer
    R16_UINT,           // RAW16 (Bayer mosaic, single-plane 16-bit)
    R10_PACKED,         // Camera2 RAW10 packed (camera HAL implementation-defined)

    // 8-bit RGB(A)
    R8G8B8A8_UNORM,
    R8G8B8_UNORM,

    // 10-bit RGB(A)
    R10G10B10A2_UNORM,

    // 16-bit float RGB(A)  -- v1 working color space default (linear Rec.2020 + FP16)
    R16G16B16A16_SFLOAT,
    R16G16B16_SFLOAT,

    // 32-bit float (calibration / debug / occasional high-precision paths)
    R32G32B32A32_SFLOAT,
    R32G32B32_SFLOAT,
    R32_SFLOAT,

    // Tensor element formats (tensor shape lives in BufferLayout::dims, not in format)
    F16,                // FP16 element
    F32,                // FP32 element
    I8,                 // INT8 (quantized)
    U8,                 // UINT8

    // Opaque byte stream (BLOB)
    BLOB
};

}  // namespace cpipe::compute
```

**Why precision is encoded in the format**: research §7 carries precision as a separate field, but this design picks `PixelFormat` as the single source of truth. `R16G16B16A16_SFLOAT` already implies "FP16, RGBA, 4 channels, 8 bytes/pixel"; `F16` implies "tensor element is FP16". This eliminates the risk of `buffer.format` and `buffer.precision` falling out of sync in a misconfigured manifest.

**Mapping to Vulkan / Metal**: each `PixelFormat` maps internally to a `VkFormat` / `MTLPixelFormat`. `UNDEFINED` corresponds to the AHB external-format path (HAL-implementation-defined Y′CbCr); v1 never receives such inputs (Bayer RAW16 always has a known format), but the entry point is preserved.

**RAW10 note**: Camera2 RAW10 is packed bytes; Vulkan has no direct format. Treat it as a storage buffer plus an unpacking kernel. See [`research/16-camera2-raw-and-burst.md`](research/16-camera2-raw-and-burst.md).

---

## 4. BufferLayout

`BufferLayout` describes the shape of an `IBuffer`. It supports four kinds:

```cpp
namespace cpipe::compute {

enum class BufferKind : uint8_t {
    Image2D = 0,       // classic image (Bayer / RGB)
    Volume3D = 1,      // 3D LUT
    TensorND = 2,      // AI tensor (NCHW / NHWC / NC / etc)
    Blob = 3           // opaque byte stream
};

struct BufferLayout {
    BufferKind   kind;
    PixelFormat  format;

    // ndim in [1, 8]
    // Image2D : ndim = 2, dims = {W, H}
    // Volume3D: ndim = 3, dims = {W, H, D}      (D = LUT edge)
    // TensorND: ndim = N, dims = {N, C, H, W}    (NCHW; semantics declared by manifest port spec)
    // Blob    : ndim = 1, dims = {bytes}
    uint8_t  ndim;
    uint32_t dims[8];

    // Stride in bytes; stride[0] is innermost.
    // Image2D's stride[1] is the row pitch; the GPU driver may pad it (Adreno 64B / Mali 16B).
    uint64_t stride[8];
};

}  // namespace cpipe::compute
```

**A single struct (with `dims/stride` arrays) instead of subclasses**: keeps `IBuffer` monomorphic; the manifest port spec declares the `kind` and dimensional rules; the scheduler validates these at load time. The result is small code, easy serialization.

**Stride is in bytes**: `vkGetImageSubresourceLayout` returns byte pitch; AHB's `desc.stride` is row in pixels — host adapters multiply by BPP to convert. This unification spares plugins from doing unit conversion themselves.

**`TensorND` layout convention**: NCHW by default, C-contiguous (`stride[0] = elem_size, stride[1] = elem_size × W, ...`). Nodes that need NHWC (some ExecuTorch / Core ML models) declare `tensor_layout: "NHWC"` in their manifest port spec, and the host inserts an automatic transpose at handoff.

---

## 5. IBuffer Interface

```cpp
namespace cpipe::compute {

class IBuffer {
public:
    virtual ~IBuffer() = default;

    // ---- Shape / metadata ----
    virtual const BufferLayout& layout() const noexcept = 0;
    virtual uint64_t            size_bytes() const noexcept = 0;

    // Per-buffer metadata snapshot (capture / calibration / processing-state /
    // color / output-sidecar / tensor-quant). Immutable; shared via shared_ptr.
    // May be null only on scratch buffers (B16). See §6.
    virtual std::shared_ptr<const BufferMetadata> metadata() const noexcept = 0;

    // ---- CPU access (CPU-device nodes only) ----
    // lock_cpu blocks until any GPU work on this buffer has completed. The buffer's
    // usage must include BUFFER_USAGE_CPU_READ or BUFFER_USAGE_CPU_WRITE.
    enum class CpuAccess { Read, Write, ReadWrite };
    virtual void* lock_cpu(CpuAccess) = 0;
    virtual void  unlock_cpu() = 0;

    // Flush CPU writes before GPU consumption (e.g. Android AHB cache flush / Mali coherency).
    // Equivalent to "unlock_cpu + signal-the-scheduler".
    virtual void  flush_cpu_writes() = 0;

    // ---- Sub-view (signature reserved for v1; not implemented) ----
    // Calling this in v1 returns nullptr and logs a warning. Tile-based v2 turns it on.
    virtual std::shared_ptr<IBuffer>
    sub_view(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) = 0;
};

}  // namespace cpipe::compute
```

**No native handles on the interface**: the `cpipe::sdk::Buffer` plugins see does not expose them either. When the host needs a native handle, it does `dynamic_cast<VulkanBuffer*>(b.get())` (or a `static_cast` after backend disambiguation). This preserves the option of a v2 sandboxing / cross-process implementation.

**Metadata accessor returns `shared_ptr<const>`**: the type itself is defined in §6; plugins reach it through `cpipe::sdk::Buffer::metadata()` (a thin wrapper over the `cpipe_metadata_suite_v1` C ABI — see [`plugin-sdk.md §3`](plugin-sdk.md)).

**What plugins see (`sdk::Buffer`)**: a C++ wrapper over `cpipe_buffer_t*` exposing `width()/height()/channels()/precision()`, `metadata()`, and `lock_cpu()`. See [`plugin-sdk.md §7`](plugin-sdk.md).

---

## 6. BufferMetadata

`BufferMetadata` is the per-buffer envelope of facts a node may need (and may extend) but that does not belong to the pixel grid: capture parameters, sensor calibration, processing-state flags, color-space role, output sidecar payloads, and AI quantization parameters. It is defined in `cpipe-core` (no Vulkan / OCIO dependency) and reaches plugins through `cpipe_metadata_suite_v1`.

### 6.1 Anatomy

```cpp
namespace cpipe::compute {

// Reverse-DNS-keyed opaque blob (EXIF / XMP / ICC / opcode bytes / vendor data).
// Held by shared_ptr so multiple buffers in a burst share a single physical copy.
struct ByteBlob {
    std::vector<std::byte> bytes;   // immutable once published
};

// Static, illuminant-aware sensor calibration. Identical across all frames of one
// burst; ingest emits a single shared_ptr<const CalibrationBlock> referenced by
// every per-frame BufferMetadata (B15).
struct CalibrationBlock {
    // CFA descriptor (cleared on demosaiced buffers via builder.clear_cfa()).
    std::optional<CFADescriptor>      cfa;

    // Black / white levels and (rare) linearization.
    std::array<float, 4>              black_level     = {};   // per-channel
    uint32_t                          white_level     = 0;
    std::optional<LinearizationTable> linearization_table;

    // Color matrices (DNG dual-illuminant).
    std::optional<Matrix3>            color_matrix1, color_matrix2;
    std::optional<Matrix3>            forward_matrix1, forward_matrix2;
    std::optional<Matrix3>            camera_calibration1, camera_calibration2;
    uint16_t                          calibration_illuminant1 = 0;   // EXIF code
    uint16_t                          calibration_illuminant2 = 0;

    // Noise model: per-channel (a, b) pairs — variance(I) = a + b·I.
    std::vector<std::pair<float,float>> noise_profile;

    // Lens calibration metadata copied from CaptureResult / DNG.
    std::array<float, 5>              lens_distortion = {};
    std::array<float, 5>              lens_intrinsic  = {};
};

// Capture-side per-frame state.
struct CaptureBlock {
    int64_t  sensor_timestamp_ns = 0;     // monotonic frame ID
    int64_t  exposure_time_ns    = 0;
    int32_t  iso                 = 0;
    float    lens_focal_length_mm   = 0.f;
    float    lens_aperture          = 0.f;
    float    lens_focus_distance_d  = 0.f;
    std::array<float, 3> as_shot_neutral = {1.f, 1.f, 1.f};   // raw-RGB WB
    uint8_t  orientation = 1;             // EXIF 1..8

    std::string camera_id;                // logical id
    std::string physical_camera_id;       // empty if logical-only
    uint32_t    burst_index = 0;
    uint32_t    burst_size  = 1;
};

// AI tensor quantization parameters. Meaningful only for BufferKind::TensorND.
struct TensorQuant {
    enum class Scheme : uint8_t { None = 0, Symmetric = 1, Asymmetric = 2 };
    Scheme               scheme = Scheme::None;
    std::optional<int8_t> axis;            // per-channel quant axis (e.g. C in NCHW)
    std::vector<float>   scales;           // size = 1 for per-tensor; size = C for per-channel
    std::vector<int32_t> zero_points;      // empty / zeroed for Symmetric
};

struct BufferMetadata {
    // ---- Identity ----
    uint64_t                                 schema_version = 1;   // bumps with suite v2

    // ---- Calibration (shared across burst) ----
    std::shared_ptr<const CalibrationBlock>  calibration;          // null if synthetic

    // ---- Capture (per-frame) ----
    CaptureBlock                             capture;

    // ---- Processing state ----
    // Reverse-DNS step IDs that have been applied to this buffer's pixels.
    // Defined v1 set: "linearization", "black_white_scaling", "opcode_list_1",
    //                 "opcode_list_2", "demosaic", "white_balance",
    //                 "opcode_list_3", "color_matrix".
    // Vendor / future steps use reverse-DNS (e.g. "vendor.qualcomm.htp_tonemap").
    std::vector<std::string>                 applied_steps;

    // ---- Color ----
    // OCIO role / cs_role string (e.g. "raw_camera", "scene_linear_rec2020",
    // "output_pq2020", "output_srgb"). Folded in from the previous color_role().
    std::string                              cs_role = "undefined";

    // Active region (in pixels) after any crop / opcode TrimBounds. Mirrors
    // DNG ActiveArea + DefaultCrop semantics.
    std::optional<Rect2u>                    active_area;

    // ---- Output sidecar (passthrough blobs) ----
    // shared_ptr so multiple buffers can share the source DNG's EXIF/XMP/ICC.
    std::shared_ptr<const ByteBlob>          exif_blob;
    std::shared_ptr<const ByteBlob>          xmp_blob;
    std::shared_ptr<const ByteBlob>          icc_blob;
    // HDR mastering / UltraHDR fields filled by the encoder side; see §6.4.
    std::optional<MasteringDisplay>          mdcv;            // SMPTE ST 2086
    std::optional<ContentLightLevel>         clli;            // MaxCLL / MaxFALL
    std::optional<UltraHdrGainMapMeta>       ultrahdr;

    // ---- Tensor quantization (TensorND only; default Scheme::None elsewhere) ----
    TensorQuant                              tensor_quant;

    // ---- Extension blobs (vendor / future / DNG opcode bytes) ----
    // Keys are reverse-DNS strings. v1 reserves:
    //   "com.cpipe.dng.opcode_list_1_bytes" / _2 / _3   (raw OpcodeList wire bytes)
    //   "com.cpipe.dng.profile_hsm"                      (hue-sat map blob)
    //   "vendor.<vendor>.<key>"                          (vendor-defined)
    std::map<std::string, std::shared_ptr<const ByteBlob>> ext_blobs;
};

}  // namespace cpipe::compute
```

The struct is **flat-by-value with shared sub-blocks**: `BufferMetadata` itself is small (the inline fields fit in a few hundred bytes); the heavy parts (calibration, blobs) are reached through `shared_ptr<const ...>` so a 10-frame burst keeps one calibration record and one EXIF blob across all buffers.

### 6.2 Lifecycle

```
ingest                   pipeline                                   sink
──────                   ────────                                   ────
 DngReader / Camera2 ─▶  Pipeline assembles graph                   HEIF
   produce               │                                          encoder
   IBuffer + meta        ▼                                          reads
        │           ┌─ for each node N ──────────────────┐          meta and
        │           │   host: builder = derive(N.in[0])  │          calls
        │           │   N.process(...) edits builder     │   ─▶     libheif
        │           │   host: freeze(builder) → output   │          add_exif /
        │           │         buffer.metadata            │          add_xmp / …
        │           └────────────────────────────────────┘
        ▼
   shared_ptr<const BufferMetadata>
```

Three points to remember:

1. **Immutable snapshot.** `IBuffer::metadata()` always returns `shared_ptr<const BufferMetadata>`; once a buffer leaves a node, its metadata is frozen forever. The next node sees the same object identity if upstream did not change anything (`shared_ptr` aliasing).
2. **Default inheritance.** Before each `process()`, the host constructs a `MetadataBuilder` for every output buffer of the node. Each builder is **default-initialized from `node.inputs[0].metadata()`** so a passthrough node needs zero metadata code. The plugin then issues only diff-style setters (`builder.set_cs_role("scene_linear_rec2020")`, `builder.add_applied_step("demosaic")`, `builder.clear_cfa()`, `builder.set_blob("vendor.x.foo", blob)`, …).
3. **Host-side freeze.** When `process()` returns, the host calls `builder.freeze()` and attaches the resulting `shared_ptr<const BufferMetadata>` to the output `IBuffer`. The builder cannot be touched after freeze.

### 6.3 Burst merge

For nodes with `cardinality: "array"` inputs (HDR fusion, temporal denoise, dust removal), the default builder still starts from `inputs[0].metadata()`. Plugins can compose using a small policy enum:

```cpp
enum class MergePolicy : uint32_t {
    Primary       = 0,  // keep base builder (already from in[0]); no-op
    MeanScalars   = 1,  // average exposure_time_ns, iso, sensor_timestamp_ns
    MedianTimestamp = 2,
    AndState      = 3,  // applied_steps = intersection across all inputs
    UnionState    = 4,  // applied_steps = union across all inputs
};

// Plugin call:
builder.merge_from(/* idx */ 1, MergePolicy::MeanScalars);
builder.merge_from(/* idx */ 1, MergePolicy::AndState);
```

The default for HDR-fusion-style nodes is therefore: primary frame's calibration / capture / state, with explicit `MeanScalars` over exposure scalars when authors want a true ensemble.

### 6.4 Output sidecar handling

Ingest copies EXIF / XMP / ICC bytes into shared `ByteBlob`s and attaches them via the metadata. Intermediate nodes do **not** rewrite these blobs — they pass through unchanged. The HEIF encoder node (the only sink in v1) reads them at `process()` and calls libheif:

```cpp
if (auto e = meta.exif_blob)  heif_context_add_exif_metadata(ctx, h, e->bytes.data(), e->bytes.size());
if (auto x = meta.xmp_blob)   heif_context_add_xmp_metadata (ctx, h, x->bytes.data(), x->bytes.size());
if (meta.mdcv)                heif_image_handle_set_mdcv    (h, *meta.mdcv);
if (meta.clli)                heif_image_handle_set_clli    (h, *meta.clli);
heif_image_handle_set_image_rotation(h, exif_rotation_for(meta.capture.orientation));
```

A node that genuinely needs to rewrite a blob (e.g. inserting `ProcessedBy: cpipe`) constructs a fresh `ByteBlob` and sets it through the builder; the previous blob is untouched. See [`research/14-heif-and-hdr-output.md §3.11`](research/14-heif-and-hdr-output.md).

### 6.5 DNG OpcodeList relationship

The DNG renderer pipeline ([`research/12-dng-format.md §3.10`](research/12-dng-format.md)) compiles `OpcodeList1/2/3` into scheduler nodes at **load time**, not into runtime metadata. `BufferMetadata.applied_steps` records that the corresponding pass ran (`"opcode_list_1"` / `"_2"` / `"_3"`); `BufferMetadata.ext_blobs["com.cpipe.dng.opcode_list_*_bytes"]` retains the **raw wire bytes** alongside, so a v2 DNG re-export node can write them back unchanged. v1 ingestion populates these blobs; v1 has no consumer, but the contract is reserved.

### 6.6 ABI evolution

- v1 frozen typed fields: everything in `CalibrationBlock`, `CaptureBlock`, `applied_steps`, `cs_role`, `active_area`, `mdcv` / `clli` / `ultrahdr`, `tensor_quant`.
- New typed fields → bump `cpipe_metadata_suite_v1` to `_v2`; v1 plugins keep working (they query the v1 suite and never see the new getters).
- New blob keys → no ABI change; just add a reverse-DNS namespaced key.
- `schema_version` mirrors the suite minor version so persisted artifacts can be detected.

### 6.7 Why this shape

- **Per-buffer metadata** matches both the burst case (every frame has a different exposure / timestamp) and the in-graph mutation case (demosaic clears CFA; white balance flips a state flag). A pipeline-run-level metadata bag would force the scheduler to track parallel tables.
- **Immutability + derive** keeps shared pointers safe across worker threads (P9) without per-buffer locks. A node modifying its input's metadata in place would be a use-after-free hazard the moment another node consumed the same buffer.
- **Mixed typed + blob ABI** keeps hot-path access (`cfa_pattern`, `noise_profile`, `cs_role`) at compile-time-checked typed getters, while EXIF / XMP / vendor data stays opaque and ABI-stable through the blob suite.
- **Calibration as a `shared_ptr` sub-block** lets a 10-frame burst keep a single physical copy without exposing two scopes to the plugin.

---

## 7. Concrete Backend Types

Every backend implements one concrete class deriving from `IBuffer`.

| Type | Platform | Native handles | Creation path |
|------|----------|----------------|----------------|
| `CpuBuffer` | all | `void* base + size` (posix_memalign / VirtualAlloc) | `Buffer::Create(layout, BUFFER_USAGE_CPU_*)` |
| `VulkanBuffer` | Linux + Android | `VkBuffer + VmaAllocation` | `Buffer::Create(layout, BUFFER_USAGE_GPU_STORAGE)` |
| `VulkanImage` | Linux + Android | `VkImage + VmaAllocation` | `Buffer::Create(layout, BUFFER_USAGE_GPU_SAMPLED)` |
| `VulkanAHBBuffer` | Android | `AHardwareBuffer*` + imported `VkBuffer` | `Buffer::ImportAhb(ahb, layout)` |
| `VulkanAHBImage` | Android | `AHardwareBuffer*` + imported `VkImage` | same; sampled usage |
| `VulkanHostPtrBuffer` | Linux | `void* host_ptr + VkBuffer` (via `VK_EXT_external_memory_host`) | `Buffer::ImportHostPtr(ptr, layout)` |
| `MetalBuffer` | macOS / iOS (v2) | `id<MTLBuffer>` over IOSurface | `Buffer::Create(...)` |
| `MetalTexture` | macOS / iOS (v2) | `id<MTLTexture>` over IOSurface | same |
| `MetalIOSurfaceBuffer` | macOS / iOS (v2) | `IOSurfaceRef` + `id<MTLTexture>` | `Buffer::ImportIOSurface(surf, layout)` |

**None of these concrete types are visible to plugins.** Plugins see `cpipe::sdk::Buffer` only.

**Same AHB across Vulkan and NPU**: on Android, a single AHB can be held simultaneously by a `VulkanAHBImage` (host-side Vulkan view) and the QNN HTP backend (host-side inference path). Two `shared_ptr<IBuffer>` then share the same underlying `AHardwareBuffer*` (refcounted via `AHardwareBuffer_acquire`). Vulkan→NPU submissions are aligned on a timeline through the `Handoff` struct (see [`research/03-heterogeneous-scheduler.md §7`](research/03-heterogeneous-scheduler.md)).

---

## 8. Creation and Allocation

Buffers are created by the host. Plugins **never** allocate `IBuffer` directly.

```cpp
namespace cpipe::compute {

enum BufferUsage : uint32_t {
    BUFFER_USAGE_INPUT          = 1u << 0,
    BUFFER_USAGE_OUTPUT         = 1u << 1,
    BUFFER_USAGE_INTERMEDIATE   = 1u << 2,
    BUFFER_USAGE_SCRATCH        = 1u << 3,

    BUFFER_USAGE_CPU_READ       = 1u << 4,
    BUFFER_USAGE_CPU_WRITE      = 1u << 5,

    BUFFER_USAGE_GPU_SAMPLED    = 1u << 6,   // VkImage + sampler
    BUFFER_USAGE_GPU_STORAGE    = 1u << 7,   // VkBuffer / RW image

    BUFFER_USAGE_NPU_INPUT      = 1u << 8,   // adds vendor flag for QNN/HTP
    BUFFER_USAGE_NPU_OUTPUT     = 1u << 9
};

struct BufferAllocator {
    // Host-owned facade. Held by the producer and the scheduler; never by plugins.
    std::shared_ptr<IBuffer> create(const BufferLayout&, BufferUsage);
    std::shared_ptr<IBuffer> import_ahb        (AHardwareBuffer*, const BufferLayout&);
    std::shared_ptr<IBuffer> import_iosurface  (IOSurfaceRef,     const BufferLayout&);  // v2
    std::shared_ptr<IBuffer> import_host_ptr   (void* p, size_t bytes, const BufferLayout&);
};

}  // namespace cpipe::compute
```

### 8.1 Internal Allocator: VMA + MTLHeap

- **Vulkan** — uses [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT, the de facto standard). VMA maintains internal sub-pools; the API is per-call (`vmaCreateBuffer / vmaCreateImage`), but allocations come out of `VkDeviceMemory` blocks. This avoids the ~1ms-per-call latency of `vkAllocateMemory` on mobile and sidesteps driver fragmentation.
- **Metal** (v2) — uses `MTLHeap` to pool `MTLBuffer / MTLTexture`. The `MTLHeap` API is itself a sub-allocator and is functionally equivalent to VMA.
- **CPU** — `posix_memalign(64-byte)` (cache line) or Windows `_aligned_malloc`.

**Why not roll our own arena**: research 03 §5 recommends a cpipe-managed arena with interference-graph coloring. This design uses VMA instead — it already implements best-fit / first-fit / linear / pool strategies and has been battle-tested on Mali / Adreno / NV / AMD / Intel, sparing us the corner cases of a hand-rolled allocator. The scheduler still computes peak memory at load time as a "will-it-fit" pre-check (the algorithm in research §5 is unchanged), but real allocations are routed through VMA.

### 8.2 External Imports

External producers (Camera2 / DngReader) bypass `BufferAllocator::create` and use the `import_*` paths:

- **AHB import** — `vkGetAndroidHardwareBufferPropertiesANDROID` + `VkImportAndroidHardwareBufferInfoANDROID`, threaded through VMA (`vmaCreateBufferWithAlignment` + `VmaAllocationCreateInfo::pUserData = AHB`). See [`research/02-zero-copy-buffer-architecture.md §3.3`](research/02-zero-copy-buffer-architecture.md).
- **IOSurface import** (v2) — `[device newTextureWithDescriptor:iosurface:plane:]`.
- **Host pointer import** — `VK_EXT_external_memory_host` + `VkImportMemoryHostPointerInfoEXT`. Requires the host pointer to be 4 KB aligned (Linux page). LibRaw's RAW16 output usually has to be copied into a page-aligned arena before it can be imported.

---

## 9. Synchronization (host-only)

Plugins **never see** `IFence` / `ITimeline`. The scheduler owns all synchronization.

```cpp
namespace cpipe::compute {

// Host-only interface (header lives in cpipe/internal/Sync.hpp, not in the plugin SDK).
class IFence {
public:
    virtual ~IFence() = default;
    virtual bool wait_host(std::chrono::nanoseconds timeout) = 0;
    virtual bool is_signaled() const = 0;
};

class ITimeline {
public:
    virtual ~ITimeline() = default;
    virtual uint64_t current_value() const = 0;
    virtual bool     wait_value(uint64_t v, std::chrono::nanoseconds timeout) = 0;
    virtual void     signal_value_host(uint64_t v) = 0;
};

}  // namespace cpipe::compute
```

Concrete implementations:

| Device | IFence | ITimeline |
|--------|--------|-----------|
| Vulkan | `VkFence` | `VkSemaphore` (timeline; `VK_KHR_timeline_semaphore`, core 1.2) |
| Metal (v2) | `dispatch_semaphore_wait` over `MTLSharedEvent` | `MTLSharedEvent` |
| CPU | `std::condition_variable_any` | `std::atomic<uint64_t>` + `condition_variable_any` |
| Hexagon NPU | wrapped FastRPC fence FD | indirectly aligned through host AHB timelines |

**Camera2 acquire-fence FD handling**: `AImageReader_acquireNextImageAsync` returns a sync_file fence FD. `Camera2BufferProducer` (host side) wraps it as a binary `VkSemaphore` via `VK_KHR_external_semaphore_fd` + `vkImportSemaphoreFdKHR` (handle type = `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR`), uses it as the first wait of the pipeline's first node, and from there hands off to the timeline semaphore. Plugins never see this conversion.

**v1 limit: batch-only**. Timeline values do not survive across pipeline runs; each `pipeline.run()` starts at timeline 0 and the timeline is released at the end. `tf::Pipeline` (TaskFlow's streaming primitive) and the token concept are reserved for v2 streaming; v1 does not enable them.

---

## 10. CPU Access Semantics

Only nodes whose `device == CPU` (DngReader / libheif encoder / debug dump nodes) call `lock_cpu`. Calling it from a GPU node is a programming error.

```cpp
auto* p = static_cast<uint16_t*>(buf->lock_cpu(CpuAccess::Write));
// ... write RAW16 pixels ...
buf->unlock_cpu();
buf->flush_cpu_writes();   // required only on the AHB path; no-op elsewhere
```

**Android AHB specifics**: an AHB written by the CPU must be explicitly flushed (cache coherency) before the GPU can see the writes. `flush_cpu_writes()` calls `AHardwareBuffer_unlock(out_fence_fd)` internally to obtain a fence FD, then registers it as a timeline value (host-side). All a plugin sees is "call flush after writing".

**Threading**: `lock_cpu / unlock_cpu` must be paired on the same thread. Touching the `void*` after `unlock` is undefined behavior.

**Access modes not supported in v1**: partial map (locking a sub-region) and persistent map (persisting the mapping across multiple uses). v1 does not need these; they are v2 considerations for tile-based mode.

---

## 11. External Producers

Producers are host classes — **not** plugin nodes. They produce `shared_ptr<IBuffer>` and feed it into pipeline input ports.

### 11.1 DngReader

```cpp
namespace cpipe::ingest {

class DngReader {
public:
    explicit DngReader(BufferAllocator& alloc);

    // Reads a DNG file and returns a single IBuffer. The returned buffer's
    // metadata() is fully populated (calibration, capture, applied_steps={},
    // cs_role="raw_camera", exif_blob, opcode_list_*_bytes). Caller does not
    // see a separate metadata struct.
    std::shared_ptr<compute::IBuffer> read(std::filesystem::path);
};

}  // namespace cpipe::ingest
```

Implementation: LibRaw 0.22 (LGPL-2.1 with static-link clause) decodes the DNG → output is copied to a page-aligned host buffer → `BufferAllocator::import_host_ptr` wraps it. The reader builds one `shared_ptr<const CalibrationBlock>` from `ColorMatrix1/2`, `ForwardMatrix1/2`, `NoiseProfile`, `BlackLevel`, `WhiteLevel`, `CFAPattern`, and copies the source EXIF / XMP / ICC and `OpcodeList1/2/3` raw bytes into shared `ByteBlob`s; the resulting `BufferMetadata` is attached to the IBuffer. The `OpcodeList1/2/3` parser is a separate ~1.5 kLoC interpreter authored under Apache 2.0 (LibRaw does not parse OpcodeList) — its compiled output drives DAG-time scheduler nodes, not runtime metadata. See [`research/12-dng-format.md`](research/12-dng-format.md).

CLI invocation:

```cpp
auto buf = ingest::DngReader(alloc).read("input.dng");
pipeline.run({{"raw", {buf}}});   // metadata travels on `buf`
```

### 11.2 Camera2BufferProducer

```cpp
namespace cpipe::ingest::android {

class Camera2BufferProducer {
public:
    explicit Camera2BufferProducer(BufferAllocator& alloc);

    // One frame ready: pulls an AImage from AImageReader, imports it as a
    // VulkanAHBBuffer, and converts the acquire-fence FD. Returns once the buffer
    // is ready at timeline value 0; the scheduler takes over from there. The
    // returned buffer's metadata() is built from CaptureResult + CameraCharacteristics.
    std::shared_ptr<compute::IBuffer> next_frame();

    // Burst: one captureBurst of N frames; returns N independent IBuffer. All N
    // frames share a single shared_ptr<const CalibrationBlock>; per-frame
    // exposure / timestamp / iso live in each buffer's CaptureBlock.
    std::vector<std::shared_ptr<compute::IBuffer>> next_burst(size_t n);
};

}  // namespace cpipe::ingest::android
```

Why this is not a plugin node:

- It needs to hold the `AImageReader` handle, the `AHardwareBuffer*` queue, and the Camera2 surface lifecycle. These are platform-specific host responsibilities, unrelated to any cpipe node.
- Plugin nodes should be platform-agnostic; pulling Camera2 into the plugin model would pollute the ABI.
- The `VK_KHR_external_semaphore_fd` conversion in research 02 §5.2 is a Vulkan-only operation that needs the host's `VkDevice` — a producer responsibility, not a plugin's.

The metadata mapping from `CaptureResult` / `CameraCharacteristics` to `BufferMetadata` is detailed in [`research/16-camera2-raw-and-burst.md §3.5 / §4.2`](research/16-camera2-raw-and-burst.md).

### 11.3 Pipeline Inputs / Outputs

`pipeline.run` signature:

```cpp
namespace cpipe {

struct RunInputs {
    // Indexed by port name; each vector holds the N IBuffer of that port
    // (N = 1 for cardinality:"single"). Per-buffer metadata travels on the
    // IBuffer itself — there is no separate metadata field on RunInputs.
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<compute::IBuffer>>> ports;
};

struct RunOutputs {
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<compute::IBuffer>>> ports;
};

class Pipeline {
public:
    RunOutputs run(const RunInputs&);
};

}  // namespace cpipe
```

At `run()` entry the host validates, for every input port marked `requires_metadata` in any downstream node manifest (see [`plugin-sdk.md §7`](plugin-sdk.md)), that the supplied buffer's `metadata()` is non-null and its `applied_steps` / `cs_role` satisfy the manifest constraints. A missing required field aborts the run with `CPIPE_NEED_METADATA` before any node executes.

Output `IBuffer`s are pre-allocated by the host at load (during memory planning); the caller receives buffers that have already been written, with their final `BufferMetadata` frozen by the host at the producing node's `process()` return. The HEIF encode node (libheif) is the sink and is responsible for writing the final `IBuffer` to disk; it reads the buffer's `BufferMetadata` to populate EXIF / XMP / orientation / mastering-display / UltraHDR fields (see §6.4).

---

## 12. Sub-view (not implemented in v1)

`IBuffer::sub_view` is reserved as a signature in v1 — calling it returns `nullptr` and logs a warning. Tile-based processing is a v2 topic:

- `VulkanImage`: `vkCreateImageView` + `VkImageSubresourceRange` (simple in v2).
- `VulkanBuffer`: arithmetic offset + new layout (simple in v2).
- IOSurface: child surface aliasing (medium-complex in v2).
- AHB: AHB itself cannot be sub-viewed; route through a Vulkan view (medium-complex in v2).

Reserving the signature lets v2 light it up without breaking the v1 ABI.

---

## 13. Memory Budget (worked example)

100 MP @ FP16 RGBA = 100e6 × 8 = **800 MB**. This is the worst-case single buffer.

| Device | GPU-reachable | Do 6 in-flight intermediates fit? |
|--------|---------------|-----------------------------------|
| Pixel 8 Pro (12 GB) | ~5–6 GB | 6 × 800 MB = 4.8 GB → tight |
| Galaxy S24 Ultra (12 GB) | ~6–7 GB | 6 × 800 MB = 4.8 GB → OK |
| iPhone 15 Pro Max (8 GB) | ~5–6 GB | tight |
| Linux desktop RTX 4070 (12 GB VRAM) | ~10 GB | OK |

Default ceiling: the scheduler plans memory at load; if `peak > device cap`, it refuses to run. Spill mode (`vkCmdCopyBufferToBuffer` to system RAM) is a v2 topic; v1 simply errors out.

**Burst inputs**: 5 RAW16 frames @ 100 MP = 5 × 200 MB = 1 GB; at 50 MP it is 500 MB. Combined with 6 in-flight intermediates, flagship devices fit; 100 MP × 10 frames forces the burst count down to 5 (manifest-configurable).

See [`research/02-zero-copy-buffer-architecture.md §8`](research/02-zero-copy-buffer-architecture.md).

---

## 14. v1 Scope Limits

Stated explicitly to prevent misuse:

- **Same-process only**: `IBuffer` does not cross process boundaries (no `SCM_RIGHTS`, no GPC handle serialization). v2 sandboxing brings these.
- **No tile-based / streaming**: a single `IBuffer` is the full image; no region-of-interest, no token-based pipeline.
- **No `plane_count > 1`**: multi-plane formats (YCbCr 4:2:0, etc.) are handled outside cpipe (libavformat / Camera HAL); buffers entering cpipe are already single-plane RGB / Bayer.
- **No hot-reload of the buffer pool**: `BufferAllocator` is not rebuilt within a pipeline lifetime.
- **No sandboxing**: plugins share the address space with the host; plugin trust = cpipe's internal security boundary.
- **No metadata stable hash**: v1 does not expose `BufferMetadata::stable_hash()`; nodes that want to cache precomputation tables across runs declare `cache_per_run` in their manifest and let the host dedupe by node-instance identity ([`plugin-sdk.md §8.5`](plugin-sdk.md)). The hash is reserved for `cpipe_metadata_suite_v2`.
- **No DNG re-export**: v1 retains `OpcodeList1/2/3` raw bytes in `BufferMetadata.ext_blobs` so a future writer can round-trip them, but no v1 node consumes the bytes.

These limits do not block v2: the architecture and ABI already reserve extension points (`sub_view`, tile dimensions, token semantics).

---

## 15. See Also

- [`plugin-sdk.md`](plugin-sdk.md) — Plugin C ABI, C++ SDK, ComputeContext, node examples.
- [`research/02-zero-copy-buffer-architecture.md`](research/02-zero-copy-buffer-architecture.md) — full backend-import derivation; Vulkan / Metal / AHB / IOSurface interop.
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — memory planning, cross-device handoff, owner of `IFence` / `ITimeline`.
- [`research/12-dng-format.md`](research/12-dng-format.md) — basis for DngReader.
- [`research/16-camera2-raw-and-burst.md`](research/16-camera2-raw-and-burst.md) — basis for Camera2BufferProducer.
