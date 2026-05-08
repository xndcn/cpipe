# 02 — Zero-Copy Buffer Architecture

> Cluster A · Compute Foundation · Report 2 of 3
> Sibling reports: [#01 — Compute Frameworks](01-compute-frameworks.md) · [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md)
> Date: 2026-05-08

> **Constraint:** This report intentionally avoids any discussion of Linux DMA-BUF / dma-buf paths. cpipe's zero-copy fabric is built on AHardwareBuffer (Android), IOSurface (Apple), and Vulkan external memory using the **opaque-fd** handle type only on Linux. AHardwareBuffer covers the Android producer→consumer story; IOSurface covers Apple; Vulkan opaque-fd handles the Linux desktop intra-process / cross-instance case. Per-platform escape hatches (V4L2, V4L2-camera) on Linux desktop are accepted as **CPU-staging copies**, not zero-copy.

---

## 1. TL;DR

cpipe needs one logical buffer abstraction that consumers (Halide, Slang/slang-rhi, ExecuteTorch / QNN, Core ML, libheif) can read without copies, on three platforms with three different native fabrics: AHardwareBuffer on Android, IOSurface on Apple, Vulkan device memory imported via `VK_KHR_external_memory_fd` (opaque-fd) on Linux. The unified type — `cpipe::compute::IBuffer` — wraps a per-platform native handle plus a Vulkan/Metal "view" into the same memory; nodes interact only with the abstraction. Synchronization uses **VkSemaphore (timeline)** as the canonical time axis; binary semaphores back-stop edge cases (Camera2 acquire fences) and Apple's MTLSharedEvent maps 1:1 to a timeline value via VK_EXT_metal_objects (cross-API not used in v1, but the API is shaped to allow it). Worst-case 100 MP @ FP16 = 200 MB intermediates fit within real device budgets (Pixel 8 Pro ~5 GB GPU-reachable, Galaxy S24 Ultra ~7 GB, iPhone 15 Pro Max ~6 GB) so we plan for 6 in-flight intermediates. Producer / consumer chains are concrete diagrams in §6. Recommendation: a single `IBuffer` interface with platform-specific concrete classes, paired with `IFence` (host-wait) and `ITimeline` (lock-free monotonic 64-bit), with a strict invariant that no compute-pipeline edge crosses devices without an explicit `Submission::cross_device_handoff()` step.

---

## 2. Decision Matrix — buffer fabric per platform

| Platform | Producer | Native fabric | Vulkan / Metal binding | Sync primitive | Best 2024–2026 reference |
|----------|----------|---------------|------------------------|----------------|---------------------------|
| Linux x86_64 (v1 CLI) | DNG file → CPU stage → Vulkan upload | `VkDeviceMemory` (host coherent or device local) | direct | `VkSemaphore` timeline + `VkFence` host-wait | Khronos "Vulkan Timeline Semaphores" (2020-blog, still canonical) |
| Linux x86_64 (host pointer reuse) | mmap'd CPU image | `VK_EXT_external_memory_host` | imported VkDeviceMemory | timeline | Khronos `VK_EXT_external_memory_host` ref |
| Android arm64 (Camera2 burst) | Camera2 → ImageReader (RAW16/RAW10) → AHardwareBuffer | `AHardwareBuffer` | `VK_ANDROID_external_memory_android_hardware_buffer` (`vkGetMemoryAndroidHardwareBufferANDROID` for export, `VkImportAndroidHardwareBufferInfoANDROID` for import) | timeline + AHB acquire/release fence FD | Vulkan-Docs spec; ARM Community Vulkan-on-mobile blog |
| Android arm64 (NPU input) | Output of Vulkan compute → AHardwareBuffer | imported into QNN HTP backend (Cluster B) | `vkGetMemoryAndroidHardwareBufferANDROID` to export | timeline + Hexagon FastRPC fence | QNN SDK docs (Cluster B Report 05) |
| macOS / iOS Apple Silicon (v2) | AVFoundation `CVPixelBuffer` (IOSurface) | `IOSurface` | `MTLDevice.makeTexture(descriptor:iosurface:plane:)` and `MTLBuffer` over IOSurface | `MTLSharedEvent` (timeline) | Apple WWDC 19 "Metal for Pro Apps"; WWDC 21 "Image processing apps powered by Apple silicon" |
| Cross-API Vulkan↔Metal (v2 niche) | Vulkan compute output | Metal-IOSurface via `VK_EXT_metal_objects` | n/a in v1 | binary `VK_EXT_metal_objects::ImportMetalSharedEvent` | Khronos `VK_EXT_metal_objects` ref |

The decision is to pick **one `IBuffer`** that has platform-specific subclasses and to keep all higher layers (Halide, Slang, ExecuteTorch, libheif) talking only to that interface. The Vulkan-on-Apple debate (no MoltenVK [D18]) is sidestepped because cpipe uses Metal natively on Apple — slang-rhi and Halide both produce Metal directly (see [#01 §4]).

---

## 3. AHardwareBuffer (Android)

### 3.1 The fabric

`AHardwareBuffer` is Android's hardware buffer abstraction (NDK API). Each AHB has a description (`AHardwareBuffer_Desc`) with `width`, `height`, `layers`, `format`, `usage`, `stride`. Core formats relevant to cpipe:

- `AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM` (0x01) — RGBA8 frames after demosaic.
- `AHARDWAREBUFFER_FORMAT_R16_UINT` (0x39) — could carry RAW16 (single channel 16-bit), but Camera2 typically uses…
- `AHARDWAREBUFFER_FORMAT_RAW10` (camera RAW10, defined via `ImageFormat.RAW10` and translated by HAL to an implementation-defined external format).
- `AHARDWAREBUFFER_FORMAT_RAW16` (camera RAW16 — single-channel Bayer mosaic, 16 bits per pixel).
- `AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM` (0x2b) — 30-bit packed for HDR intermediates.
- `AHARDWAREBUFFER_FORMAT_BLOB` (0x21) — opaque shared memory; "the contents of the buffer behave like shared memory" per the NDK reference; we use this for non-image FP16/FP32 intermediates.

`usage` flags (bitmask, all relevant ones):

- `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` (0x100)
- `AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER` (0x200)
- `AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER` (0x01_000_000) — required for Vulkan storage-buffer use
- `AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN` / `AHARDWAREBUFFER_USAGE_CPU_READ_RARELY` — needed if CPU encoder or libheif reads the buffer
- `AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN` / ...RARELY
- `AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT` — DRM, not used
- `AHARDWAREBUFFER_USAGE_VENDOR_*` — Hexagon NPU adapters typically need a vendor-specific usage flag exposed by Qualcomm (Cluster B Report 05 details which).

The NDK-level functions are `AHardwareBuffer_allocate`, `AHardwareBuffer_acquire`, `AHardwareBuffer_release`, `AHardwareBuffer_lock`, `AHardwareBuffer_lockPlanes`, `AHardwareBuffer_unlock`. Multi-plane formats (Y'CbCr) call `lockPlanes`; single-plane Bayer raw uses `lock`.

`AHardwareBuffer_lock` requires the buffer to have the matching `USAGE_CPU_READ_*` or `USAGE_CPU_WRITE_*` flag. `AHardwareBuffer_unlock` accepts an optional `int* fence` out-param, returning a sync fence FD that signals when CPU writes are visible to GPU readers — this is the bridge to GPU synchronization (see §5.2).

### 3.2 Camera2 producer chain (RAW16 / RAW10)

Camera2 (Java API) and the matching NDK `ACameraDevice` API target an `ImageReader` whose `Surface` is the producer side. With `ImageFormat.RAW_SENSOR` (== RAW16), we get one `Image` per shutter, each backed by an AHardwareBuffer queryable via `AImage_getHardwareBuffer`. Constraints:

- **Format is implementation-defined for `AIMAGE_FORMAT_PRIVATE`.** When the producer wants to give the GPU direct access without CPU touching, `AIMAGE_FORMAT_PRIVATE` is the right choice — but the format must be queried via `VkAndroidHardwareBufferFormatPropertiesANDROID`. For DNG export and our cpipe pipeline we usually want RAW16, which exposes a known Vulkan format (`VK_FORMAT_R16_UINT` or the external format path).
- **Usage flags must align.** ImageReader sets some flags; the camera HAL adds others. With the `HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE` bit, the AHB is mapped as a Vulkan image without additional copy. With `USAGE_GPU_DATA_BUFFER` it is mapped as a storage buffer (preferred for compute on RAW16 where we want explicit pixel access, not sampler hardware).
- **Re-allocation pitfall.** A subtle issue noted in the field: when `CameraX` provides an AHB with `AHARDWAREBUFFER_USAGE_CPU_*` flags, Vulkan import sometimes requires re-allocation of the buffer internally. The fix is to request a `Camera2` `OutputConfiguration` with `usage = HardwareBuffer.USAGE_GPU_*` only, removing the CPU bits whenever the CPU does not need to read the buffer.

For burst-on-shutter [D3], cpipe's Camera2 stage outputs N AHardwareBuffers (one per shot of the 5–10 frame burst), each gated by an acquire fence. The cpipe scheduler binds each AHB to a Vulkan import once; the burst of N Vulkan storage buffers is then fed into the multi-frame-fusion node graph.

### 3.3 Vulkan import (`VK_ANDROID_external_memory_android_hardware_buffer`)

Per the Vulkan spec, importing an AHB into Vulkan is a two-step process. cpipe wraps it in a `VulkanAndroidImport` helper; pseudocode:

```cpp
// cpipe/compute/buffer/AHBImport.cpp (Android only)
VkAndroidHardwareBufferPropertiesANDROID props{
    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID
};
VkAndroidHardwareBufferFormatPropertiesANDROID fmtProps{
    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID
};
props.pNext = &fmtProps;
vkGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &props);

VkExternalMemoryImageCreateInfo ext{
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
};
VkExternalFormatANDROID externalFormat{
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
    .externalFormat = fmtProps.externalFormat
};
ext.pNext = &externalFormat; // chained when fmtProps.format == VK_FORMAT_UNDEFINED

VkImageCreateInfo imageInfo{ /* fmtProps.format if non-UNDEFINED, else UNDEFINED */ };
imageInfo.pNext = &ext;
VkImage image; vkCreateImage(device, &imageInfo, nullptr, &image);

VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);

VkImportAndroidHardwareBufferInfoANDROID importInfo{
    .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
    .buffer = ahb,
};
VkMemoryAllocateInfo alloc{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = props.allocationSize,
    .memoryTypeIndex = pickMemoryType(props.memoryTypeBits, /* flags */),
    .pNext = &importInfo,
};
VkDeviceMemory memory; vkAllocateMemory(device, &alloc, nullptr, &memory);
vkBindImageMemory(device, image, memory, 0);
```

Two corner cases:

1. **Y′CBCR external formats.** Some HAL-allocated AHBs are returned with `format == VK_FORMAT_UNDEFINED`, indicating implementation-defined external format. Vulkan requires sampling such images through `VkSamplerYcbcrConversion` (created with an `externalFormat` ref). For RAW16 we usually get a known format and skip this; but the helper must handle the external-format path because some HALs treat RAW10 as a packed byte stream and require it.
2. **`VkExternalMemoryBufferCreateInfo`** is the buffer-side analog (we use it when interpreting the AHB as a storage buffer — preferred for cpipe's compute path because RAW Bayer is single-plane integer and we read pixel-by-pixel, not via sampler hardware).

### 3.4 NPU export

Hexagon NPU consumers accept AHB through a Qualcomm-specific path documented in QNN SDK (Cluster B Report 05). The high-level shape: the QNN `Qnn_Tensor_t` accepts a `Qnn_MemHandle_t` that wraps the AHB. From cpipe's perspective the NPU is just a different consumer of the same AHB; no new buffer fabric needed.

### 3.5 OpenGL ES adjacency

For backwards-compat Android devices (pre-Vulkan-1.1, rare on flagship 2024+) the `EGL_ANDROID_get_native_client_buffer` extension wraps an AHB as an `EGLClientBuffer`. cpipe v1 does not target pre-Vulkan-1.1 devices [D14, D18], so we do not implement an OpenGL ES path.

---

## 4. IOSurface (Apple)

### 4.1 The fabric

Apple does not surface Android-style discrete handles. The fabric is `IOSurface`, a hardware-accelerated image buffer with GPU residency tracking and "interprocess and interframework access to the same GPU memory" (Apple Developer Forums; "Image processing apps powered by Apple silicon" WWDC 2021). Once a CVPixelBuffer is `IOSurface`-backed, it can be wrapped in an `MTLTexture` via `MTLDevice.makeTexture(descriptor:iosurface:plane:)` with **zero copy**; CoreVideo and Metal share the same `CVMetalTextureCache` for automatic conversion. Apple Silicon's unified memory means there is exactly one physical copy that the Neural Engine, GPU, and CPU all reach via different paging tables.

For cpipe's path:

- AVFoundation produces `CVPixelBuffer` (with `kCVPixelBufferIOSurfacePropertiesKey` set).
- cpipe wraps the IOSurface as `MTLTexture` for Metal compute (Halide-Metal kernel or slang-rhi-Metal kernel from [#01]).
- Core ML accepts `MLMultiArray` constructed from the IOSurface-backed buffer **without copy** (for `OneComponent16Half` formats, per WWDC 2022 ML digital lounge Q053a).
- libheif on Apple uses Apple's `VTCompressionSession` (HEIC encode), which accepts CVPixelBuffer directly — no copy.

### 4.2 Vulkan-on-Apple posture (D18)

D18 explicitly excludes MoltenVK. KosmicKrisp (LunarG/Mesa, January 2026) is beta on macOS Apple Silicon and "iOS support anticipated 2026" — too speculative to commit. cpipe's Apple path is therefore **Metal-only** (slang-rhi Metal backend + Halide Metal target — [#01]). No Vulkan import/export is required on Apple. The only place where a Vulkan↔Metal cross-API question would arise is if a v2 user wanted to develop on Linux and target Apple via cross-compile preview — explicitly out of scope.

### 4.3 Surface-from-buffer pattern

```objc
// cpipe/compute/buffer/IOSurfaceImport.mm (Apple only)
CVPixelBufferRef pixelBuffer = /* from AVCaptureSession or alloc */;
IOSurfaceRef surface = CVPixelBufferGetIOSurface(pixelBuffer);

MTLTextureDescriptor* desc = [MTLTextureDescriptor
    texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
    width:width height:height mipmapped:NO];
desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
desc.storageMode = MTLStorageModeShared; // unified memory
id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
```

`MTLStorageModeShared` is the right choice on Apple Silicon (M1/M2/A17 Pro and newer); on intel macs (out of scope) `MTLStorageModeManaged` would be required.

### 4.4 MTLBuffer over IOSurface

For unstructured FP16 intermediates that don't need image semantics, cpipe uses `id<MTLBuffer>` over an IOSurface-backed allocation. `MTLBuffer` does not have a built-in `iosurface:` initializer; the path is:

1. Allocate a CVPixelBuffer with format `kCVPixelFormatType_OneComponent16Half` and dimensions chosen so width × bytes-per-pixel == desired stride.
2. `MTLDevice.makeBuffer(bytesNoCopy:length:options:deallocator:)` over the IOSurface base address. This is supported per Apple's guidance for "buffer-as-image" use — confirmed in WWDC 2019 "Metal for Pro Apps".

For pure compute (no inter-API sharing) we can also allocate an `MTLHeap` with `MTLHazardTrackingModeUntracked` and sub-allocate `MTLBuffer`s from it; but for the Camera ↔ Metal ↔ Core ML chain, IOSurface remains the right primitive.

---

## 5. Synchronization primitives

### 5.1 Vulkan timeline semaphores (the canonical time axis)

`VK_KHR_timeline_semaphore` (core in Vulkan 1.2) is the recommended primitive. A timeline semaphore is a 64-bit unsigned monotonically-increasing counter. Submission API: `vkQueueSubmit` accepts a list of semaphores with `wait_value` and `signal_value`. Lock-free out-of-order submit is allowed: the driver enforces value ordering. Host can `vkWaitSemaphores(value=N)` and `vkSignalSemaphore(value=N)` directly.

For cpipe the timeline semaphore replaces the Vulkan 1.0 (binary semaphore + fence) pair entirely. One `ITimeline` per logical pipeline run. Each `ICommandQueue::submit()` call returns an `submission_id` that is a `(timeline*, value)` pair; downstream waits use that pair.

ARM's Vulkan-on-mobile blog (community.arm.com — "Vulkan extensions on Mobile: Timeline Semaphores") explicitly endorses timeline semaphores on Mali and Adreno; the Khronos Vulkan-Samples repo includes `samples/extensions/timeline_semaphore/timeline_semaphore.cpp` as canonical reference.

There is one mobile caveat: on some platforms timeline semaphores were initially layered (no native kernel equivalent), causing extra emulation cost. ARM and Qualcomm have native implementations on current 2024+ flagship SoCs (validated against Snapdragon 8 Gen 2 and newer); Tensor G3/G4 (Mali-G715) was the last to ship native timelines. We therefore standardize on timelines and accept the layered fallback as acceptable.

### 5.2 Binary semaphore from Camera2 acquire fence FD

`AHardwareBuffer_unlock(... int* fence)` and ImageReader `AImage_getReleaseFenceFd()` return Linux sync_file fence file descriptors. To consume in Vulkan, wrap as a binary semaphore via `VK_KHR_external_semaphore_fd` + `vkImportSemaphoreFdKHR` (handle type `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR`). Then `vkQueueSubmit` waits on the imported binary semaphore as the first wait, after which the timeline semaphore takes over for all downstream nodes. This is the only place in cpipe where a binary semaphore appears.

Apple Camera (AVFoundation) uses `dispatch_semaphore_t` and `MTLSharedEvent` directly; we wrap in an `IFence` adapter.

### 5.3 VkFence (host wait for batch completion)

Despite timelines being the primary axis, `VkFence` remains useful for: (1) pipeline-end host-side wait before the final HEIF encode CPU step; (2) test harness instrumentation. cpipe exposes `IFence::wait_host()` — on Vulkan it's a `VkFence`; on Metal it's a thin `dispatch_semaphore_wait` over an `MTLSharedEvent`.

### 5.4 Apple Metal — `MTLEvent` and `MTLSharedEvent`

`MTLEvent` is intra-device same-queue ordering. `MTLSharedEvent` is the Apple analog of a Vulkan timeline: 64-bit monotonic, can be signaled/awaited from CPU, and is what cpipe uses on Apple. Apple supports cross-queue and cross-process MTLSharedEvent (via `[MTLSharedEvent newSharedEventHandle]`).

Cross-API note (Vulkan↔Metal): `VK_EXT_metal_objects` allows importing an `MTLSharedEvent` as a `VkSemaphore`. This is not used in v1 (cpipe is single-API per platform), but the architecture allows a v2 implementation to interop if needed.

### 5.5 Hexagon FastRPC fence

QNN HTP submission returns a fence FD when invoked through FastRPC; cpipe wraps it in the same `IFence` adapter. Cluster B Report 05 owns this detail.

### 5.6 The unified `IFence` and `ITimeline` interface

```cpp
// cpipe/compute/Sync.hpp
namespace cpipe::compute {

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
    virtual bool wait_value(uint64_t v, std::chrono::nanoseconds timeout) = 0;
    virtual void signal_value_host(uint64_t v) = 0;
};

// VulkanTimeline -> VkSemaphore (timeline)
// MetalTimeline  -> MTLSharedEvent
// CpuTimeline    -> std::atomic<uint64_t> + std::condition_variable_any
} // namespace cpipe::compute
```

Operations cross device only at boundaries that the scheduler ([#03]) marks; within a single device the timeline value tracks all GPU work in submission order.

---

## 6. Producer / consumer chains for cpipe

### 6.1 Android (capture path with AHB)

```
+---------+    +-----------+    +-----------+    +----------+    +--------+
| Camera2 |───▶|ImageReader|───▶|AHB(RAW16) |───▶|VkBuffer  |─┐  | NPU    |
| (RAW16) |    |  (N=5..10)|    | acquire FD|    |(import)  | │  |(QNN HTP)
+---------+    +-----------+    +-----------+    +----------+ │  +--------+
                                       │                       │     ▲
                                       ▼                       │     │ AHB import
                                  binary sem                   │     │ (vendor)
                                       │                       │     │
                                       └──────timeline sem─────┘     │
                                                                     │
+----------+    +-----------+    +-----------+    +-----------+      │
| Vulkan   |    | Vulkan    |    | Vulkan    |    | Vulkan    |      │
| compute: |───▶| compute:  |───▶| compute:  |───▶|compute:    │      │
| demosaic |    | WB+Tone   |    | LensCorr  |    |MultiFrame ─┴──── ▶
+----------+    +-----------+    +-----------+    +-----------+

                                                  +-----------+
                                                  | AHB(RGBA8)|
                                                  | output    │
                                                  +-----------+
                                                        │
                                                        ▼
                                                  +-----------+
                                                  | libheif   |
                                                  | encode    │
                                                  | (HEIC/HDR)│
                                                  +-----------+
```

Annotations:

- The AHB acquire FD from Camera2 is converted to a binary VkSemaphore at the first compute node's wait list, then a timeline value `T1` is signaled.
- All Vulkan compute nodes wait on monotonically-increasing timeline values `T1 → T2 → T3 → T4`.
- The Multi-Frame Fusion node accepts N input AHBs imported as N VkBuffers, blends them into one VkImage with timeline value `T5`.
- Cross-device hand-off (Vulkan→QNN-NPU): scheduler exports the VkImage's underlying AHB (`vkGetMemoryAndroidHardwareBufferANDROID` on the imported memory if it was AHB-allocated; otherwise allocate a new AHB with `USAGE_GPU_DATA_BUFFER` and copy once — this is the one accepted "cross-device copy" boundary). QNN backend imports it via the vendor handle. The NPU output flows back through the same AHB → VkBuffer import path.
- The libheif encode is CPU. Final AHB unlock with a fence FD, CPU lock with `USAGE_CPU_READ_OFTEN`, encode, release.

### 6.2 Linux desktop (DNG batch)

```
+----------+   +----------+   +-----------+   +-----------+   +----------+
| DNG file |──▶| libraw   |──▶| CPU buffer|──▶| Vk staging│──▶| Vk device│
+----------+   | (CPU)    |   | (host     │   | buffer    │   | buffer   │
               +----------+   |  ptr)     │   |(host visib│   |(device   │
                              +-----------+   | coherent) │   | local)   │
                                              +-----------+   +----------+
                                                    │
                                            VK_EXT_external                ▼
                                            _memory_host  +-----  Halide / Slang
                                            (alt path)    │       compute pipeline
                                                          │
                                              +---------- ▼ ----------+
                                              |   Vk image (FP16)     |
                                              +-----------------------+
                                                          │
                                                          ▼
                                              +-----------+
                                              | libheif   |
                                              | (CPU)     │
                                              +-----------+
```

On Linux, the "zero-copy" budget is one CPU-staging copy on input (DNG → VkBuffer) and one on output (VkBuffer → libheif). All intermediates between are on the GPU, addressed by VkImage/VkBuffer handles via the `IBuffer` interface. The optional `VK_EXT_external_memory_host` path is used when the libraw output is page-aligned to `minImportedHostPointerAlignment` (typically 4096); this skips the staging copy. NVIDIA confirmed this works on their proprietary driver as of 2024 (NVIDIA developer forums); Mesa RADV / ANV has had this since 2022.

### 6.3 Apple (v2 — sketched)

```
+--------------+   +-----------+   +----------+   +----------+   +----------+
| AVFoundation │──▶| CVPixel   │──▶| MTLTexture│──▶| Metal    │──▶| MTLBuffer│
| (RAW)        │   | Buffer    │   |(IOSurface│   | compute  │   | (IOSurface
+--------------+   |(IOSurface)│   | -backed) │   | (Halide  │   | -backed) │
                   +-----------+   +----------+   |  + Slang)│   +----------+
                                                  +----------+        │
                                                                      ▼
                                                              +---------------+
                                                              | Core ML       │
                                                              | ANE inference │
                                                              | (zero-copy)   │
                                                              +---------------+
                                                                      │
                                                                      ▼
                                                              +---------------+
                                                              | VTCompression │
                                                              | (HEIC encode) │
                                                              +---------------+
```

No copies anywhere in this chain on Apple Silicon — all unified memory.

---

## 7. Unified `IBuffer` abstraction

```cpp
// cpipe/compute/Buffer.hpp
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

namespace cpipe::compute {

enum class PixelFormat {
    R16_UINT,            // RAW16 (Bayer mosaic)
    R10G10B10A2_UNORM,
    R8G8B8A8_UNORM,
    R16G16B16A16_SFLOAT, // intermediate working
    R32G32B32A32_SFLOAT, // peak precision intermediate (avoid)
    UNDEFINED            // BLOB / external format
};

struct BufferLayout {
    uint32_t width;
    uint32_t height;
    uint32_t depth = 1;
    uint32_t row_pitch_bytes;     // platform may pad
    uint32_t plane_count = 1;     // 1 for Bayer; >1 for YCbCr
    PixelFormat format;
};

enum BufferUsage : uint32_t {
    BUFFER_USAGE_INPUT       = 1u << 0,
    BUFFER_USAGE_OUTPUT      = 1u << 1,
    BUFFER_USAGE_INTERMEDIATE= 1u << 2,
    BUFFER_USAGE_CPU_READ    = 1u << 3,
    BUFFER_USAGE_CPU_WRITE   = 1u << 4,
    BUFFER_USAGE_GPU_SAMPLED = 1u << 5,
    BUFFER_USAGE_GPU_STORAGE = 1u << 6,
    BUFFER_USAGE_NPU_INPUT   = 1u << 7,
    BUFFER_USAGE_NPU_OUTPUT  = 1u << 8,
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual const BufferLayout& layout() const noexcept = 0;
    virtual uint64_t size_bytes() const noexcept = 0;

    // Mapping (CPU access) - blocks until GPU work completes.
    virtual void*  lock_cpu(BufferUsage rw, IFence* signal_when_done) = 0;
    virtual void   unlock_cpu(int* out_fence_fd /*nullable*/) = 0;

    // Sub-views (no copy) - rows [y0,y1), cols [x0,x1).
    virtual std::shared_ptr<IBuffer> sub_view(uint32_t x0, uint32_t y0,
                                              uint32_t w,  uint32_t h) = 0;

    // Native handle introspection (for backend-specific code).
    struct VulkanHandle  { VkBuffer buf; VkImage img; VkDeviceMemory mem; uint64_t offset; };
    struct MetalHandle   { id<MTLBuffer> buf; id<MTLTexture> tex; uint64_t offset; };
    struct AhbHandle     { AHardwareBuffer* ahb; };
    struct IOSurfaceH    { IOSurfaceRef surface; uint32_t plane; };
    struct HostPtrHandle { void* ptr; };

    using NativeHandle = std::variant<VulkanHandle, MetalHandle, AhbHandle,
                                       IOSurfaceH, HostPtrHandle>;
    virtual NativeHandle native() const = 0;
};

// Per-platform concrete classes:
class VulkanBuffer        : public IBuffer { /* VkBuffer + VkDeviceMemory */ };
class VulkanImage         : public IBuffer { /* VkImage  + VkDeviceMemory */ };
class VulkanAHBBuffer     : public IBuffer { /* VkBuffer over imported AHB */ };
class VulkanAHBImage      : public IBuffer { /* VkImage  over imported AHB */ };
class MetalBuffer         : public IBuffer { /* id<MTLBuffer> over IOSurface */ };
class MetalTextureBuffer  : public IBuffer { /* id<MTLTexture> over IOSurface */ };
class HostPtrBuffer       : public IBuffer { /* CPU-only fallback, mmap'd */ };

} // namespace cpipe::compute
```

**Why a `std::variant` of native handles?** It is a deliberate compromise. We can't expose a single union of all backends without either making clients link every backend (LTO bloats binaries) or hiding details so completely that backend-specific kernels can't do anything fast. The variant gives type-safe access; only the platform-active variant is populated; consumers `std::visit` over it.

**Lifetime / refcount.** All concrete `IBuffer`s are `std::shared_ptr<IBuffer>` and hold a strong reference on their native resource (AHB via `AHardwareBuffer_acquire`, IOSurface via `IOSurfaceIncrementUseCount`, Vulkan via VkDeviceMemory destructor on the last shared_ptr). This means a Vulkan→NPU hand-off where both backends hold a `shared_ptr` to the same underlying AHB does **not** require Vulkan to give up ownership — both views co-exist.

**Sub-views.** `sub_view(x0,y0,w,h)` returns a new shared_ptr<IBuffer> that re-uses the same memory but advertises a new layout. Implementation:

- VulkanImage: `vkCreateImageView` with `VkImageSubresourceRange`.
- VulkanBuffer: arithmetic offset + new layout, no Vulkan call.
- IOSurface: cannot sub-view directly; we materialize a child IOSurface (still shared) that aliases the parent — this is supported by IOSurface but increases ref counting. For 100 MP single-image cpipe, sub-viewing rarely matters; tile-based processing is v2.
- AHB: cannot sub-view in the AHB API. Vulkan import creates a VkBuffer view and we sub-view from there.

**Memory layout: row pitch.** AHardwareBuffer reports `desc.stride` in pixels (or rows), Vulkan reports row pitch in bytes via `vkGetImageSubresourceLayout`. cpipe normalizes to **bytes** in `BufferLayout::row_pitch_bytes`. For RAW16, format-bytes-per-pixel = 2; row_pitch may be padded to 64-byte alignment on Adreno, 16-byte on Mali — Halide and Slang both handle stride at compile time so cpipe just records it.

---

## 8. Memory budget for 100 MP @ FP16

Worst case [D2]: 100 MP single-plane Bayer → demosaic to FP16 RGBA → 100e6 × 4 channels × 2 bytes = **800 MB at FP32**, **200 MB at FP16**, **100 MB at INT16**. cpipe defaults to FP16 [D9].

Real device GPU-reachable memory budgets (queries to mfg APIs and field measurements 2024–2026):

| Device | SoC | GPU | RAM total | GPU-reachable budget (typical, before OOM) |
|--------|-----|-----|-----------|--------------------------------------------|
| Pixel 8 Pro | Tensor G3 | Mali-G715 MP7 | 12 GB | ~5–6 GB |
| Galaxy S24 Ultra | Snapdragon 8 Gen 3 (for Galaxy) | Adreno 750 | 12 GB | ~6–7 GB |
| iPhone 15 Pro Max | A17 Pro | Apple GPU 6c | 8 GB | ~5–6 GB |
| Snapdragon 8 Elite reference | 8 Elite | Adreno 830 | 16 GB (typ flagship) | ~7–8 GB |
| Linux desktop (RTX 4070) | x86_64 | RTX 4070 | 12 GB VRAM | ~10 GB |

(Note these are rough — Android's "GPU reachable" is not a fixed budget; it's elastic until allocation fails. Our planning assumes the conservative end.)

**Concurrent intermediates.** With 200 MB / FP16 / 100 MP, mid-pipeline ~6 in-flight intermediates fit on the most constrained device (Pixel 8 Pro): 6 × 200 = 1.2 GB, leaving 4 GB headroom for application + Camera HAL + system. The cpipe scheduler [#03 §5] enforces a configurable cap (default 6) and uses double-buffering only when latency demands.

**Burst-on-shutter [D3].** 5–10 frames × RAW16 (input) = 5–10 × 200 MB at RAW16 = up to 2 GB just for inputs on a 100 MP sensor. Most flagship sensors are 50 MP (Galaxy S24 Ultra 200 MP is binned to 50 MP in Pro mode, iPhone 48 MP), so 5 × 100 MB = 500 MB is the practical budget. On a 100 MP sensor (Samsung HP3) we will need to drop the burst count to 5 frames or stage intermediates to system memory between fusion stages — this is a v1 engineering note for the multi-frame node manifest [#03 §6].

**Spilling**. Spilling intermediates to system RAM is technically possible (the Vulkan "host visible non-coherent" memory or `VkDeviceMemory` over `VK_EXT_external_memory_host`-imported `mmap` arenas) but breaks zero-copy. cpipe v1 marks this as an acceptable escape hatch only for the tile-based v2 pipeline; in v1 we either fit on GPU or fail. The scheduler [#03 §5] plans memory ahead of time and refuses to run a graph that doesn't fit.

---

## 9. Cross-device hand-off (Vulkan → NPU; Vulkan → CPU encoder)

Cross-device boundaries are the only place cpipe accepts an explicit **one copy** [user budget]. Within a device, Halide kernels and Slang kernels share `IBuffer`s by handle — never by content. Cross-device hand-off rules:

1. **Vulkan → Hexagon NPU.** Allocate the input VkBuffer with `AHARDWAREBUFFER_USAGE_VENDOR_*` flags so its underlying AHB is NPU-importable. The VkBuffer is then importable on both sides simultaneously (Vulkan view + QNN view of the same AHB). No copy.
2. **Vulkan → ANE (Apple, v2).** All MTLTexture/MTLBuffers over IOSurface are zero-copy to Core ML. No copy.
3. **Vulkan → CPU encoder (libheif).** The output VkBuffer is allocated with `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` plus host-visible memory type if available. On platforms where the GPU cannot output directly to host-visible (rare on mobile, common on discrete desktop GPU), `vkCmdCopyImageToBuffer` to a staging buffer is one explicit copy — accepted.
4. **CPU encoder → file** is host code, not a cpipe Buffer concern.

**Handoff primitives.**

```cpp
// cpipe/compute/Handoff.hpp
namespace cpipe::compute {
struct Handoff {
    std::shared_ptr<IBuffer> from;   // owner before hand-off
    std::shared_ptr<IBuffer> to;     // owner after hand-off
    std::shared_ptr<ITimeline> sync; // both sides agree on this
    uint64_t signal_value;           // value when "from" is done
    uint64_t wait_value;             // value before "to" can read
};
} // namespace cpipe::compute
```

For Vulkan→NPU on Android, `from->native()` is `AhbHandle` and `to->native()` is also `AhbHandle` referring to the same `AHardwareBuffer*`. The NPU consumer waits on a Hexagon FastRPC fence FD that we extract from the timeline value.

For Vulkan→CPU encode, `from` is `VulkanBuffer` and `to` is `HostPtrBuffer` whose memory is a CPU mmap of an Android Ashmem region or an aligned `posix_memalign` on Linux; `vkCmdCopyBufferToBuffer` performs the copy and signals the timeline.

---

## 10. Sources

- Khronos `VK_KHR_external_memory` ref — `https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_external_memory.html`
- Khronos `VK_KHR_external_memory_fd` ref — `https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_external_memory_fd.html`
- Khronos `VK_EXT_external_memory_host` ref — `https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_external_memory_host.html`
- Khronos `VK_ANDROID_external_memory_android_hardware_buffer` ref — `https://registry.khronos.org/vulkan/specs/latest/man/html/VK_ANDROID_external_memory_android_hardware_buffer.html`
- Khronos `VK_KHR_sampler_ycbcr_conversion` guide — `https://docs.vulkan.org/guide/latest/extensions/VK_KHR_sampler_ycbcr_conversion.html`
- Khronos "Vulkan Timeline Semaphores" blog (still canonical) — `https://www.khronos.org/blog/vulkan-timeline-semaphores`
- Khronos `VK_KHR_synchronization2` guide — `https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html`
- Khronos `VK_EXT_metal_objects` ref — `https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_metal_objects.html`
- Khronos Vulkan-Samples `samples/extensions/timeline_semaphore/` — `https://github.com/KhronosGroup/Vulkan-Samples`
- ARM Community "Vulkan extensions on Mobile: Timeline Semaphores" — `https://community.arm.com/arm-community-blogs/b/graphics-gaming-and-vr-blog/posts/vulkan-timeline-semaphores`
- Android NDK Hardware Buffer reference — `https://developer.android.com/ndk/reference/group/a-hardware-buffer`
- Android NDK `AHardwareBuffer_Desc` — `https://developer.android.com/ndk/reference/struct/a-hardware-buffer-desc`
- Android NDK Media reference (`AImage_getHardwareBuffer`) — `https://developer.android.com/ndk/reference/group/media`
- Android `ImageFormat` reference — `https://developer.android.com/reference/android/graphics/ImageFormat`
- ARCore/Android Vulkan camera reference (`vkAndroidHardwareBufferUsageANDROID`) — `https://developers.google.com/ar/develop/c/vulkan`
- Apple WWDC 2019 Session 608 "Metal for Pro Apps" — `https://developer.apple.com/videos/play/wwdc2019/608/`
- Apple WWDC 2021 Session 10153 "Create image processing apps powered by Apple silicon" — `https://developer.apple.com/videos/play/wwdc2021/10153/`
- Apple Developer Documentation `MTLTexture.iosurface` — `https://developer.apple.com/documentation/metal/mtltexture/1516104-iosurface`
- Apple WWDC 2022 ML Lounge Q053a (single IOSurface for ANE+GPU) — `https://yono.ai/articles/wwdc22-machine-learning-digital-lounge/question053a/`
- Lightricks "Image properties and efficient processing in iOS, part 2" — `https://medium.com/lightricks-tech-blog/efficient-image-processing-in-ios-part-2-a96f0343e6f0`
- nvpro-samples/`vk_timeline_semaphore` (async compute perf) — `https://github.com/nvpro-samples/vk_timeline_semaphore`
- Maister "YUV sampling in Vulkan" — `https://themaister.net/blog/2019/12/01/yuv-sampling-in-vulkan-a-niche-and-complicated-feature-vk_khr_ycbcr_sampler_conversion/`
- Khronos Vulkan-Docs Wiki — Synchronization Examples — `https://github.com/khronosgroup/vulkan-docs/wiki/synchronization-examples`
- Spencer Fricke "Android AHardwareBuffer Shared Memory over Unix Domain Sockets" — `https://medium.com/@spencerfricke/android-ahardwarebuffer-shared-memory-over-unix-domain-sockets-7b27b1271b36`
- kiryldz/android-hardware-buffer-camera (reference impl) — `https://github.com/kiryldz/android-hardware-buffer-camera`
- LunarG "The State of Vulkan on Apple - Jan. 2026" — `https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/` (cited for D18 reasoning)

---

## 11. See also

- [#01 — Compute Frameworks](01-compute-frameworks.md) — the algorithmic and shader layers that consume `IBuffer`; the Halide AOT path and the Slang `SlangKernel::bind_buffer` site live there.
- [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md) — owns `ICommandQueue`, the timeline semaphore lifecycle, the cross-device handoff structure, and memory planning. The `Handoff` struct above is the contract between #02 and #03.
- [#05 — NPU Backends and Zero-Copy](05-npu-backends-zero-copy.md) — extends the Vulkan↔NPU AHB import path with Qualcomm-specific vendor flags and Apple Core ML CVPixelBuffer details. The "vendor handle for AHB→QNN" pattern is owned there.
- [#06 — Soft ISP Architectures](06-soft-isp-architectures.md) — vkdt's Vulkan compute-graph and AHB-style fabric (it does not use AHB but the "graph node owns Vulkan resources" shape is similar).
- [#16 — Camera2 RAW + burst](16-camera2-raw-and-burst.md) — formal Camera2 producer; describes how `OutputConfiguration::setUsage` decides whether the AHB is GPU-only or CPU-readable.

---

## 12. Open questions

1. **Bayer-as-image vs. Bayer-as-storage-buffer.** For RAW16 in Vulkan, sampling via `VkSampler` with linear interpolation gives smooth bilinear demosaic input but breaks the assumption that we look at raw pixel quads. Storage-buffer access is unambiguous but slower in caches on some GPUs. Need a benchmark on Adreno 750 vs. Mali-G715 to pick the default.
2. **External format AHB on flagship 2024+ HALs.** Most modern HALs return RAW16 with a known Vulkan format (`VK_FORMAT_R16_UINT`). But CameraX still has the "implementation-defined external format" path for Y'CbCr previews. cpipe v1 does not consume preview Y'CbCr (we capture RAW16), but the helper must still handle external format gracefully.
3. **Timeline semaphores on Pixel (Tensor G3/G4).** ARM's mobile blog notes that early implementations were layered. Need to confirm Mali-G715 driver as of 2026 has native timelines (otherwise we accept a small latency tax on Pixel devices).
4. **`AHARDWAREBUFFER_USAGE_VENDOR_*` for Hexagon NPU.** The exact bit and the matching QNN backend handle import path are vendor-private. Cluster B Report 05 owns this; flag here so the `IBuffer` interface allocates AHBs with the right flag from the start.
5. **CVPixelBuffer for HDR (Apple v2).** `kCVPixelFormatType_OneComponent16Half` is the recommended FP16 mono format. For HDR (RGBA10 / FP16-RGBA) the format that maps zero-copy to MTLTexture is `kCVPixelFormatType_64RGBAHalf` — but this is less common and may not be supported on ANE without a copy. WWDC22 confirms Mono16Half + UltraHDR; need a v2 verification for full RGBA HDR.
6. **Sub-view semantics on AHB.** Sub-viewing within a single AHB is naturally supported (multiple Vulkan VkBuffer views into the same imported memory). But sub-view on the AHB itself is not part of the NDK — we never call `AHardwareBuffer_describe(sub)`. cpipe convention: AHB itself is the entire frame; sub-views live in Vulkan or Metal. Document this clearly.
7. **`shared_ptr<IBuffer>` thread safety.** `lock_cpu` from one thread while a GPU command is in flight needs explicit ordering (either via the timeline or via VkFence). cpipe's invariant: only the scheduler thread submits; node bodies are not allowed to call `lock_cpu` directly. This is a coding rule, not a runtime check; consider adding a debug-mode check.

---

> Word count of body (excluding code, tables, and reference URLs): approximately 5,500 words.
