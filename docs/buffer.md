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
| B5 | Native handles (VkBuffer / AHardwareBuffer / MTLTexture) are **not** exposed across the C ABI; plugins submit work through `ComputeContext` only | Host-side C++ may still unpack natives — see §6 |
| B6 | CPU access goes through `lock_cpu / unlock_cpu / flush_cpu_writes` in the C ABI buffer suite | Only CPU-device nodes (libheif encoder, libraw reader) call these |
| B7 | `IBuffer::sub_view` reserved as a v1 signature only; not implemented | Tile-based processing is a v2 topic |
| B8 | Internal allocator = VMA + MTLHeap; external imports (AHB / IOSurface / host pointer) go through VMA's import path | API is per-call; backend is sub-pool |
| B9 | DngReader / Camera2BufferProducer are decoupled from `Pipeline`; not plugin nodes | They produce `IBuffer` as host classes that feed pipeline input ports |
| B10 | Plugins see `cpipe::sdk::Buffer` backed by `shared_ptr<IBuffer>` | Native ownership is shared across backends (e.g. Vulkan + NPU on the same AHB) |
| B11 | OCIO role is queryable on a Buffer but not settable; color transforms live in dedicated color nodes | The manifest's `color.input_role / output_role` is declarative |
| B12 | v1 scope limits: same-process only; no tile-based / streaming / hot-reload / sandboxing | Architecture does not block v2; v1 simply does not implement these |

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

- **Producer** (DngReader / Camera2BufferProducer) — host classes, not plugin nodes. They produce `shared_ptr<IBuffer>` to feed pipeline input ports before `pipeline.run(inputs)` is invoked.
- **Pipeline** — a DAG of node instances; each node is a plugin (see [`plugin-sdk.md`](plugin-sdk.md)). Edges carry `shared_ptr<IBuffer>`.
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
    R16_SFLOAT,        // single-channel Bayer-domain FP16 intermediate (2 bytes/pixel)

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

    // OCIO role string (e.g. "scene_linear", "rec2020_linear").
    // The Buffer is read-only here; color transforms live in dedicated color nodes.
    virtual std::string_view    color_role() const noexcept = 0;

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

**What plugins see (`sdk::Buffer`)**: a C++ wrapper over `cpipe_buffer_t*` exposing only `width()/height()/channels()/precision()/color_role()` and `lock_cpu()`. See [`plugin-sdk.md §7`](plugin-sdk.md).

---

## 6. Concrete Backend Types

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

## 7. Creation and Allocation

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

### 7.1 Internal Allocator: VMA + MTLHeap

- **Vulkan** — uses [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT, the de facto standard). VMA maintains internal sub-pools; the API is per-call (`vmaCreateBuffer / vmaCreateImage`), but allocations come out of `VkDeviceMemory` blocks. This avoids the ~1ms-per-call latency of `vkAllocateMemory` on mobile and sidesteps driver fragmentation.
- **Metal** (v2) — uses `MTLHeap` to pool `MTLBuffer / MTLTexture`. The `MTLHeap` API is itself a sub-allocator and is functionally equivalent to VMA.
- **CPU** — `posix_memalign(64-byte)` (cache line) or Windows `_aligned_malloc`.

**Why not roll our own arena**: research 03 §5 recommends a cpipe-managed arena with interference-graph coloring. This design uses VMA instead — it already implements best-fit / first-fit / linear / pool strategies and has been battle-tested on Mali / Adreno / NV / AMD / Intel, sparing us the corner cases of a hand-rolled allocator. The scheduler still computes peak memory at load time as a "will-it-fit" pre-check (the algorithm in research §5 is unchanged), but real allocations are routed through VMA.

### 7.2 External Imports

External producers (Camera2 / DngReader) bypass `BufferAllocator::create` and use the `import_*` paths:

- **AHB import** — `vkGetAndroidHardwareBufferPropertiesANDROID` + `VkImportAndroidHardwareBufferInfoANDROID`, threaded through VMA (`vmaCreateBufferWithAlignment` + `VmaAllocationCreateInfo::pUserData = AHB`). See [`research/02-zero-copy-buffer-architecture.md §3.3`](research/02-zero-copy-buffer-architecture.md).
- **IOSurface import** (v2) — `[device newTextureWithDescriptor:iosurface:plane:]`.
- **Host pointer import** — `VK_EXT_external_memory_host` + `VkImportMemoryHostPointerInfoEXT`. Requires the host pointer to be 4 KB aligned (Linux page). LibRaw's RAW16 output usually has to be copied into a page-aligned arena before it can be imported.

---

## 8. Synchronization (host-only)

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

## 9. CPU Access Semantics

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

## 10. External Producers

Producers are host classes — **not** plugin nodes. They produce `shared_ptr<IBuffer>` and feed it into pipeline input ports.

### 10.1 DngReader

```cpp
namespace cpipe::ingest {

class DngReader {
public:
    explicit DngReader(BufferAllocator& alloc);

    // Reads a DNG file; returns a RAW16 IBuffer + metadata (CFA pattern, color matrices, opcode lists).
    struct DngFrame {
        std::shared_ptr<compute::IBuffer> raw;     // R16_UINT, BufferKind::Image2D
        DngMetadata                       meta;    // CFA / WB / opcode lists
    };

    DngFrame read(std::filesystem::path);
};

}  // namespace cpipe::ingest
```

Implementation: LibRaw 0.22 (LGPL-2.1 with static-link clause) decodes the DNG → output is copied to a page-aligned host buffer → `BufferAllocator::import_host_ptr` wraps it. The `OpcodeList1/2/3` parser is a separate ~1.5 kLoC interpreter authored under Apache 2.0 (LibRaw does not parse OpcodeList). See [`research/12-dng-format.md`](research/12-dng-format.md).

CLI invocation:

```cpp
auto frame = ingest::DngReader(alloc).read("input.dng");
pipeline.run(/* in0 */ {frame.raw}, /* meta */ frame.meta);
```

### 10.2 Camera2BufferProducer

```cpp
namespace cpipe::ingest::android {

class Camera2BufferProducer {
public:
    explicit Camera2BufferProducer(BufferAllocator& alloc);

    // One frame ready: pulls an AImage from AImageReader, imports it as a
    // VulkanAHBBuffer, and converts the acquire-fence FD. Returns once the buffer
    // is ready at timeline value 0; the scheduler takes over from there.
    std::shared_ptr<compute::IBuffer> next_frame();

    // Burst: one captureBurst of N frames; returns N independent IBuffer.
    std::vector<std::shared_ptr<compute::IBuffer>> next_burst(size_t n);
};

}  // namespace cpipe::ingest::android
```

Why this is not a plugin node:

- It needs to hold the `AImageReader` handle, the `AHardwareBuffer*` queue, and the Camera2 surface lifecycle. These are platform-specific host responsibilities, unrelated to any cpipe node.
- Plugin nodes should be platform-agnostic; pulling Camera2 into the plugin model would pollute the ABI.
- The `VK_KHR_external_semaphore_fd` conversion in research 02 §5.2 is a Vulkan-only operation that needs the host's `VkDevice` — a producer responsibility, not a plugin's.

### 10.3 Pipeline Inputs / Outputs

`pipeline.run` signature:

```cpp
namespace cpipe {

struct RunInputs {
    // Indexed by port name; each vector holds the N IBuffer of that port (N = 1 for cardinality:"single").
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<compute::IBuffer>>> ports;
    DngMetadata meta;   // ColorMatrix / NoiseProfile / etc. at ingest time
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

Output `IBuffer`s are pre-allocated by the host at load (during memory planning); the caller receives buffers that have already been written. The HEIF encode node (libheif) is the sink and is responsible for writing the final `IBuffer` to disk.

---

## 11. Sub-view (not implemented in v1)

`IBuffer::sub_view` is reserved as a signature in v1 — calling it returns `nullptr` and logs a warning. Tile-based processing is a v2 topic:

- `VulkanImage`: `vkCreateImageView` + `VkImageSubresourceRange` (simple in v2).
- `VulkanBuffer`: arithmetic offset + new layout (simple in v2).
- IOSurface: child surface aliasing (medium-complex in v2).
- AHB: AHB itself cannot be sub-viewed; route through a Vulkan view (medium-complex in v2).

Reserving the signature lets v2 light it up without breaking the v1 ABI.

---

## 12. Memory Budget (worked example)

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

## 13. v1 Scope Limits

Stated explicitly to prevent misuse:

- **Same-process only**: `IBuffer` does not cross process boundaries (no `SCM_RIGHTS`, no GPC handle serialization). v2 sandboxing brings these.
- **No tile-based / streaming**: a single `IBuffer` is the full image; no region-of-interest, no token-based pipeline.
- **No `plane_count > 1`**: multi-plane formats (YCbCr 4:2:0, etc.) are handled outside cpipe (libavformat / Camera HAL); buffers entering cpipe are already single-plane RGB / Bayer.
- **No hot-reload of the buffer pool**: `BufferAllocator` is not rebuilt within a pipeline lifetime.
- **No sandboxing**: plugins share the address space with the host; plugin trust = cpipe's internal security boundary.

These limits do not block v2: the architecture and ABI already reserve extension points (`sub_view`, tile dimensions, token semantics).

---

## 14. See Also

- [`plugin-sdk.md`](plugin-sdk.md) — Plugin C ABI, C++ SDK, ComputeContext, node examples.
- [`research/02-zero-copy-buffer-architecture.md`](research/02-zero-copy-buffer-architecture.md) — full backend-import derivation; Vulkan / Metal / AHB / IOSurface interop.
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — memory planning, cross-device handoff, owner of `IFence` / `ITimeline`.
- [`research/12-dng-format.md`](research/12-dng-format.md) — basis for DngReader.
- [`research/16-camera2-raw-and-burst.md`](research/16-camera2-raw-and-burst.md) — basis for Camera2BufferProducer.
