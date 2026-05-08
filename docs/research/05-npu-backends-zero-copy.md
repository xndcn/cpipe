# 05 — NPU Backends & GPU↔NPU Zero-Copy

> Cluster B research, scope: deep dive on Qualcomm Hexagon and Apple Neural Engine, plus the cross-API synchronisation primitives that let cpipe chain GPU and NPU work without redundant copies. Companion report: [04 — Mobile AI Inference Engines](04-mobile-ai-inference.md). Date of writing: 2026-05-08.

---

## 1. TL;DR

cpipe targets **Qualcomm Hexagon HTP** (Snapdragon flagships from 8 Gen 3 onwards) and **Apple Neural Engine** (A14+ / M1+) per D13. In 2026 the right SDK entry points are:

- **Qualcomm**: the **QAIRT SDK** (Qualcomm AI Runtime), which absorbed and replaced QNN SDK + SNPE SDK from late 2024. The runtime artefact cpipe links is `libQnnHtp.so`, distributed under Qualcomm's proprietary EULA (redistributable but **not** Apache-2.0 source); we dynamically link to it. AOT context binaries (`*.bin`) are produced once per (model, SoC, QAIRT version) tuple and cached on device. **Zero-copy CPU↔NPU** is achieved through the `rpcmem` / `libcdsprpc` shared-memory allocator, surfaced as the `HtpSharedMemoryAllocator` in both ExecuTorch and ONNX Runtime QNN EP. **AHardwareBuffer cannot be DMA'd by HTP directly** — one memcpy from AHB to rpcmem is unavoidable in 2026.
- **Apple**: the canonical entry is **Core ML 8** with `MLProgram` (MIL); the framework dispatches to ANE (FP16-only), GPU, or CPU depending on `MLComputeUnits`. **Zero-copy GPU↔ANE** is achieved through **IOSurface-backed `CVPixelBuffer`** with format `OneComponent16Half` or `OneComponent16` plus paddings. The ANE driver shares the IOSurface page with the GPU; an `MLMultiArray` constructed from a pixel buffer routes through without copy.

The cross-cutting recommendation: cpipe owns a **`InferenceContext` C++ abstraction** and a **per-platform `BufferAdapter`** (one for AHB↔rpcmem on Android, one for IOSurface↔CVPixelBuffer on Apple). The abstraction is what plugins talk to; everything else is implementation. MediaTek and Samsung get a 1-paragraph adapter contract each per D13.

---

## 2. Decision Matrix

### 2.1 Per-platform NPU stack

| Platform | NPU | Primary SDK (2026) | Format on disk | Zero-copy CPU↔NPU mechanism | License of runtime | cpipe primary engine |
|---|---|---|---|---|---|---|
| **Android (Snapdragon 8 Gen 3 / 8 Elite)** | Hexagon HTP V73 / V75 / V79 | **QAIRT 2.43** (≈ QNN SDK + SNPE) | QNN context binary `*.bin` | `rpcmem` shared memory via `libcdsprpc.so` | Qualcomm proprietary (redistributable) | ExecuTorch QNN backend |
| **macOS / iOS (A14+, M1+)** | Apple Neural Engine | **Core ML 8** + MIL | `*.mlmodelc` | IOSurface-backed `CVPixelBuffer` | Apple SDK (system framework) | ExecuTorch Core ML backend (or Core ML direct) |
| **Android (MediaTek Dimensity)** | MediaTek MDLA | NeuroPilot Express SDK | `*.dla` | NeuroPilot allocator (`mtk_*`) | MediaTek proprietary | ExecuTorch MediaTek backend (out of priority for v1) |
| **Android (Samsung Exynos)** | Samsung NPU | Exynos AI Studio + ENN SDK | `*.nnc` | ENN shared buffers | Samsung proprietary; ONE is Apache-2.0 | TBD (out of priority for v1) |

### 2.2 Buffer-flow paths (input feed)

| Input source | GPU step | NPU step (Hexagon) | NPU step (ANE) | # copies |
|---|---|---|---|---|
| `AHardwareBuffer` (Camera2) | `VkImage` import via `VK_ANDROID_external_memory_android_hardware_buffer` | memcpy AHB → rpcmem (HTP DMA's rpcmem) | n/a | **1** |
| `CVPixelBuffer` (AVFoundation) | `MTLTexture` from `IOSurface` | n/a | `MLMultiArray.init(pixelBuffer:)` | **0** |
| Host-allocated buffer | upload to `VkBuffer` / `MTLBuffer` | memcpy → rpcmem | wrap in CVPixelBuffer + IOSurface | 1 (Android), 0–1 (Apple) |
| Output of upstream GPU node | `VkBuffer` / `MTLTexture` | memcpy → rpcmem (or `VK_QCOM_image_processing` extension) | IOSurface bridging | 1 (Android), 0 (Apple) |

### 2.3 Sync primitives

| Edge | Primitive |
|---|---|
| Vulkan → Vulkan | `VkSemaphore` / timeline semaphore (`VK_KHR_timeline_semaphore`, core in 1.2) |
| Vulkan → CPU | timeline semaphore on `VkQueue::Submit` + `vkWaitSemaphoresKHR` |
| Vulkan → HTP | (no direct fence) — CPU-side wait on Vulkan, then submit HTP via QNN; HTP completion is a kernel callback on rpcmem context |
| HTP → Vulkan | CPU-side QNN callback signals a `VkSemaphore` from the host |
| Metal → Metal | `MTLEvent` / `MTLSharedEvent` for cross-queue / cross-process |
| Metal → ANE | indirect — `MLPredictionOptions` + completion handler on the shared dispatch queue; the framework guarantees ordering when the same IOSurface is used |
| ANE → Metal | completion handler signals an `MTLSharedEvent` or `dispatch_semaphore` |

---

## 3. Detailed Findings

### 3.1 Qualcomm Hexagon

#### 3.1.1 The SDK landscape — what is QNN, SNPE, QAIRT, AI Hub, Hexagon NN?

A 2026 reader gets confused by overlapping names; here is the canonical map.

* **QAIRT (Qualcomm AI Runtime) SDK** — the umbrella. From late 2024 the SDK ships under this name. It contains: the QNN runtime libraries (`libQnnHtp.so`, `libQnnCpu.so`, `libQnnGpu.so`, `libQnnDsp.so`); the legacy SNPE runtime; the Genie LLM helper; and conversion / context-binary / profiler tools. Latest as of 2026-05-08 is **QAIRT 2.43**, deployed on the AI Hub Workbench. ([QAIRT setup](https://docs.qualcomm.com/nav/home/general_setup.html?product=1601111740009302), accessed 2026-05-08; [AI Hub release notes](https://workbench.aihub.qualcomm.com/docs/hub/release_notes.html).)
* **QNN (Qualcomm Neural Network) SDK** — historical name; tools and libraries now ship as part of QAIRT. The runtime API headers (`QnnTypes.h`, `QnnHtp.h`, `QnnInterface.h`) remain — what ExecuTorch's Qualcomm backend, ORT's QNN EP, LiteRT's QNN delegate, and MNN's QNN backend all dlopen. The headers are public; the binaries are EULA-licensed.
* **SNPE (Snapdragon Neural Processing Engine) SDK** — predecessor; uses the **DLC (Deep Learning Container)** model format. Still functional and shipped inside QAIRT for backward compatibility but Qualcomm's recommendation is to migrate to QNN. ORT's SNPE EP is being deprecated.
* **AI Hub / AI Hub Workbench** — Qualcomm's online service: cloud profiler, model zoo (`qualcomm/ai-hub-models`), and compilation service. Useful during model authoring; **not** in the cpipe runtime path. The Workbench rebranding happened in early 2026; all the interesting models (MobileNet, EfficientNet, ESPCN, AINR-NN) are downloadable as either ONNX, TFLite, or QNN context binary, with measured latency on Snapdragon SKUs.
* **Hexagon NN** — pre-QNN low-level kernel library; pre-dates QNN, mostly historical. Out of scope for cpipe.
* **Hexagon SDK** — a different SDK for writing custom DSP kernels in C/C++ on Hexagon (e.g. ISP firmware). Not what cpipe uses for AI inference.

**Implication for cpipe**: depend on QAIRT 2.43 headers for the QNN API; runtime libraries dynamically loaded; ship a small loader that reports a useful error if the libraries are missing. Pin to a min-supported QAIRT (2.40) so the same context binary works across the 2026 OEM-shipped versions.

#### 3.1.2 Hexagon HTP generations and Snapdragon SoC mapping

The HTP (Hexagon Tensor Processor) is the NN-accelerator subsystem of the Hexagon DSP. Generation labels (V73, V75, V79) are LLVM target identifiers — they identify the ISA, not the SoC name.

| SoC | Marketing | HTP / Hexagon | Approx. NPU TOPS | Year |
|---|---|---|---|---|
| SM8550 | Snapdragon 8 Gen 2 | V73 (HTP) | ~26 INT8 | 2022 |
| SM8650 | Snapdragon 8 Gen 3 | V75 (HTP) | ~45 INT8 | 2023 |
| SM8750 | Snapdragon 8 Elite (Gen 4 nominal) | V79 (HTP) | ~45+ INT8 | 2024–25 |
| SM8850 / SM8950 | Snapdragon 8 Elite Gen 5 | V81/V82 (HTP) (LLVM patches in flight) | rumoured ~70 INT8 | 2025–26 |

Sources: [Wikipedia Qualcomm Hexagon](https://en.wikipedia.org/wiki/Qualcomm_Hexagon); [LLVM PR #120983 V79](https://github.com/llvm/llvm-project/pull/120983); [Snapdragon 8 Elite product brief](https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Elite-Platform-Product-Brief.pdf); [Chipsandcheese deep-dive on Hexagon NPU](https://chipsandcheese.com/p/qualcomms-hexagon-dsp-and-now-npu).

The HTP is heterogeneous: a **scalar** Hexagon, a **vector** unit (HVX, 1024-bit SIMD), and a **tensor** unit (HMX, matrix-multiply). cpipe AI ISP nodes spend most cycles in HMX. A context binary compiled for V75 will not run on V73 or V79 — versioning the cache key by HTP generation is mandatory.

#### 3.1.3 Quantisation sweet spots

The HMX supports **INT2, INT4, INT8, INT16, FP8, FP16** as of the 8 Elite (V79). Practical rules ([emergentmind.com on 8 Elite HTP](https://www.emergentmind.com/topics/qualcomm-sm8750-ab-snapdragon-8-elite-hexagon-tensor-processor-htp); [arxiv mobile NPU LLM paper, 2025](https://arxiv.org/html/2509.23324v1)):

* **INT8 weight + INT8 activation** is the throughput maximum; HMX peak operations per cycle are 4× FP16. Use this when accuracy budget allows.
* **INT16 activation + INT8 weight** ("A16W8") is the accuracy sweet spot for ISP — preserves enough headroom for a denoise residual without falling off the throughput cliff.
* **INT4 weight + INT8/INT16 activation** is for LLM-class models; ISP CNNs rarely benefit because weight tensors are small (a 1.5 M-param demosaic doesn't need bandwidth-saving INT4).
* **FP16** runs but at a lower rate — keep for nodes where the quantisation perceptual loss is unacceptable (e.g., final tone-mapping logit).

#### 3.1.4 AOT compilation flow (QNN context binary)

The QNN `qnn-context-binary-generator` tool consumes a quantised graph (typically produced by ORT's quantizer or AIMET) and emits a `*.bin` that the runtime can load with no further compilation. Important properties:

* Binary is **operating-system agnostic, device-specific**. Same `.bin` runs on Android and Linux but only on the SoC it was compiled for.
* QAIRT 2.4x adds **link jobs**: combine multiple models into one `.bin` and share weights — saves disk space when shipping a denoise + demosaic pair, both with the same backbone.
* DLC (SNPE format) is hardware-agnostic — backwards compatible across QAIRT versions, but the runtime compiles to the device-specific representation on first load. cpipe should prefer QNN context binary for fastest cold-start; DLC is fine for development.

cpipe's build pipeline:
```
ONNX (FP32, from PyTorch export)
  → ONNX-quantized.onnx (INT8/A16W8 via AIMET or ORT.quantization)
  → qnn_model_lib_generator → libQnnModel_<name>.so (compiled C++ representation of the graph)
  → qnn_context_binary_generator --backend libQnnHtp.so → context_<soc>.bin
```
The `.bin` lives in a per-SoC subdirectory of the cpipe assets folder.

#### 3.1.5 Zero-copy: rpcmem / libcdsprpc

The HTP is on a separate physical address space from the application processor, served by the **CDSP (compute DSP) RPC layer**. Buffers DMA'd by the HTP must be allocated through `rpcmem_alloc()` from `libcdsprpc.so`; the allocator returns a pointer the CPU can read/write *and* the HTP can see through its DMA path, sharing the same physical pages.

ONNX Runtime exposes this via its `HtpSharedMemoryAllocator`. Setting `enable_htp_shared_memory_allocator=1` in QNN EP options causes ORT to allocate input / output tensors through the allocator and pass the rpcmem buffer to the QNN graph register API ([PR #23136](https://github.com/microsoft/onnxruntime/pull/23136), [PR #23892](https://github.com/microsoft/onnxruntime/pull/23892)). ExecuTorch's QNN backend has the equivalent — its `qnn_executorch_backend` calls `rpcmem_alloc` for input/output slots and registers the memory handles with the QNN context.

**The AHardwareBuffer gap.** AHB is allocated through `gralloc`; it lives in DMA-able physical memory but, unless the gralloc instance and the rpcmem instance happen to allocate from the same ION heap (which is *not* a contract — it's an implementation accident on certain devices), the HTP cannot DMA from AHB pages directly. The supported path is to allocate an rpcmem buffer of the same size and memcpy the AHB contents in. For a 100 MP RAW10 input the copy is ~165 MB (12.5 MP planar @ 16 bpp) — at typical memory bandwidth this is ~30 ms which is wall-time-significant.

**Workarounds being explored**:
1. **`VK_QCOM_image_processing` + Vulkan-resident weight tensors** — Qualcomm's Vulkan extension exposes built-ins like `OpImageSampleWeightedQCOM`, `OpImageBlockMatchSADQCOM` that effectively let you run NN-like workloads on the GPU using "weight images" backed by Vulkan device memory. This is the right path for *Vulkan*, not HTP. It does not bridge AHB to HTP.
2. **`QNN GPU` backend** — the GPU-resident QNN execution provider (Qualcomm's blog on the QNN GPU backend, May 2025: <https://www.qualcomm.com/developer/blog/2025/05/unlocking-power-of-qualcomm-qnn-execution-provider-gpu-backend-onnx-runtime>) targets Adreno via a different backend library (`libQnnGpu.so`) — useful when the model layout is "GPU-friendly", and importantly Vulkan / Adreno buffers don't need a memcpy. Performance on AI denoise is worse than HTP but the no-copy path is real. cpipe should consider `QNN GPU` as a complementary backend when input is a Vulkan buffer that has not been routed through rpcmem.
3. **Ongoing ORT work** — [PR #24195](https://github.com/microsoft/onnxruntime/pull/24195) "Enable mapping of buffers allocated on CPU in the NPU address space" hints at NPU-side mapping of CPU pages, which would, if extended to AHB, eliminate the memcpy. Not landed as of 2026-05-08; track for v1.5.

For v1, accept the single memcpy. It costs predictable latency and works on every Snapdragon device that ships QAIRT.

#### 3.1.6 Latency / throughput / power for ISP-class models

Combined from public benchmarks ([Qualcomm AI Hub model zoo](https://github.com/qualcomm/ai-hub-models/releases), [chipsandcheese](https://chipsandcheese.com/p/qualcomms-hexagon-dsp-and-now-npu), and academic literature; numbers are SoC-typical, not device-guaranteed):

| Model class | Params | Input | 8 Elite (V79) HTP @ INT8 | 8 Gen 3 (V75) HTP @ INT8 | Power |
|---|---|---|---|---|---|
| AI demosaic (light, 1.5 M, DemosaicNet) | 1.5 M | 12 MP Bayer | 50–60 ms | 80–90 ms | ~1.4 W |
| AI denoise (NAFNet-tile, 7 M) | 7 M | 1080p tile | 12–16 ms | 22–26 ms | ~1.6 W |
| AI HDR multi-frame (10 M) | 10 M | 5× 1080p | 100–120 ms | 180–220 ms | ~2.0 W |
| Single fp16 op (worst) | n/a | 4096² | runs at half int8 throughput | same | thermal-bound |

These fit the v1 batch budget (D5). For 100 MP inputs (D2) the model must be tiled at 1080p–2160p; the AI denoise budget approaches half a second at 100 MP. AI HDR with 5× 100 MP frames is not realistic on a single phone in v1; the architecture must accept that AI HDR stays at preview-grade resolution then up-samples to full-res with classic algorithms (a soft constraint we surface to [08](08-ai-isp-algorithms.md)).

#### 3.1.7 Integration with ExecuTorch QNN backend / ORT QNN EP

| Decision | ExecuTorch | ORT |
|---|---|---|
| Ahead-of-time tooling | `executorch.backends.qualcomm` Python package | `qnn-context-binary-generator` directly |
| Model precision input | PyTorch model + PT2E quant config | quantised ONNX file |
| Soc selection | `soc_model="SM8750"` | `htp_arch=v75` and `device_id` in EP options |
| Library link | dlopen `libQnnHtp.so` from QAIRT | dlopen `libQnnHtp.so` from QAIRT |
| Shared memory allocator | yes, automatic in v1.2 | yes, opt-in `enable_htp_shared_memory_allocator=1` |
| Build size | +1.1 MB to ET runtime + libQnnHtp.so (~5 MB) | +1.5 MB to ORT + libQnnHtp.so |

**cpipe ratifies ExecuTorch as primary** for the same reason as in [04](04-mobile-ai-inference.md) §3.1: PyTorch-native authoring, single artifact (`.pte`), partitioner that gracefully degrades to CPU. ORT remains the escape hatch when a model exists only as ONNX or when ExecuTorch's partitioner refuses an op subgraph that ORT happens to handle.

### 3.2 Apple Neural Engine

#### 3.2.1 Core ML 8 deployment in 2026

Core ML in 2026 is at version **8** (shipped with iOS 18 / macOS 15 in 2024, evolved through 26-Q1). Authoring is via **`coremltools` 8.x** (Python). The recommended model representation is **`MLProgram`**, which is a programmatic IR (MIL — Model Intermediate Language) backed by a `.mlpackage` (a folder, packaged into `.mlmodelc` once compiled). The legacy `NeuralNetwork` format is restricted to a subset of ops and does not get the latest ANE features — avoid for new work.

Compute precision policy ([Apple coremltools typed-execution docs](https://apple.github.io/coremltools/docs-guides/source/typed-execution.html), accessed 2026-05-08):
* `MLProgram` ops can declare `compute_precision` of `FP16`, `FP32`, or mixed.
* On the ANE, only **FP16** is executed. FP32 ops fall back to GPU (Metal) or CPU.
* Quantised INT8 / palettised weights are *internally* dequantised to FP16 by the ANE driver — this is bandwidth saving rather than compute saving.

#### 3.2.2 Where ANE actually executes

Selection is via `MLModelConfiguration.computeUnits` on Swift / `MLComputeUnits` on Objective-C / Core ML EP options on ONNX Runtime:

| Setting | Behaviour |
|---|---|
| `.cpuOnly` | force CPU |
| `.cpuAndGPU` | CPU + Metal; ANE skipped |
| `.cpuAndNeuralEngine` | CPU + ANE; **GPU not used** |
| `.all` (default) | system decides — ANE preferred for FP16 conv; GPU for image-shaped FP32 ops; CPU fallback |

For cpipe, the right default is `.cpuAndNeuralEngine` for AI ISP nodes whose precision manifest declares FP16 (D9) — this avoids surprising fallback to GPU when the ANE would do, while keeping CPU as the scalar tail. Nodes whose manifest declares FP32 should use `.cpuAndGPU` (the GPU is FP32-capable; the ANE is not). `.all` is fine for v1 prototype but make the choice explicit per node.

#### 3.2.3 IOSurface and zero-copy ANE

The ANE communicates with the rest of the SoC through **IOSurface** — Apple's shared-memory primitive that is the foundation of CoreVideo, CoreImage, Metal, and the camera capture pipeline. ANE I/O uses IOSurface-backed buffers in a `[1, channels, 1, spatial]` layout in FP16, with a 64-byte alignment on the last axis ([Orion paper, arxiv 2603.06728](https://arxiv.org/abs/2603.06728); [Apple WWDC22 ML lounge](https://yono.ai/articles/wwdc22-machine-learning-digital-lounge/question053a/); [hollance/neural-engine docs](https://github.com/hollance/neural-engine)). The same IOSurface can simultaneously back a Metal texture, a `CVPixelBuffer`, and an `MLMultiArray` — **the binding is what changes, not the bytes**.

Practical recipes:

```swift
// (a) Camera capture → CVPixelBuffer (IOSurface-backed by AVFoundation default).
let pb: CVPixelBuffer = sample.imageBuffer!

// (b) Wrap as MLMultiArray for ANE; no copy.
let mlArray = try MLMultiArray(pixelBuffer: pb,
                               shape: [1, 4, height, width])

// (c) Feed Core ML model — runs on ANE.
let pred = try model.prediction(input: ModelInput(input: mlArray))

// (d) Use the same IOSurface as a Metal texture for downstream GPU node.
let descriptor = MTLTextureDescriptor.texture2DDescriptor(
    pixelFormat: .r16Float, width: width, height: height, mipmapped: false)
let mtlTex = device.makeTexture(descriptor: descriptor,
                                iosurface: CVPixelBufferGetIOSurface(pb)!.takeUnretainedValue(),
                                plane: 0)!
```

Constraints that bite cpipe:
* **Pixel format**. `OneComponent16Half` (`kCVPixelFormatType_OneComponent16Half`) and `OneComponent16` are supported for direct ANE shareability. RGBA8 and YpCbCr are not directly shareable; conversion is required.
* **Layout**. ANE wants `[1, C, 1, S]`; image data is naturally `[1, H, W, C]` or `[1, C, H, W]`. The `MLMultiArray(pixelBuffer:shape:)` convenience handles small images; for larger ones the framework can rearrange in-place if the IOSurface stride matches, otherwise it copies. Test on cpipe's actual tensor shapes before declaring a path zero-copy.
* **Min size**. IOSurface enforces a ~49 KB minimum, so very small tensors get padded. Irrelevant for ISP shapes (always large), relevant for any micro-tensor metadata.

#### 3.2.4 Sharing a Metal texture with Core ML

Two routes:
1. **Through CVPixelBuffer** as above — the textbook path.
2. **Through `MPSGraph`** — Metal Performance Shaders Graph is a separate API that can run models authored in MIL but stays inside Metal. It does not target the ANE; it stays on the GPU. cpipe should not use it for ANE-targeted models. It is useful as a Metal-only fallback when the ANE is busy or unavailable.

The path for cpipe AI nodes that already have GPU output is therefore: GPU produces an `MTLTexture` → wrap the `IOSurface` as a `CVPixelBuffer` → wrap as `MLMultiArray` → submit to Core ML.

#### 3.2.5 Metal 4 and ML encoders

Apple's WWDC25 announcement of Metal 4 added `MTLStageMachineLearning` — a pipeline stage that runs Core ML packages directly inside a Metal command buffer, with the same sync primitives (barriers, fences, events) as compute and render. This is significant: it means a Metal-rendered buffer can be tone-mapped by an ML encoder in the same command buffer, with native sync, without leaving Metal. The ML encoder can dispatch to ANE under the hood when the model is amenable.

cpipe v1 (Apple is v2 per D1) should design the abstraction so a Metal-4 ML encoder is the eventual Apple zero-copy path; for v2 launch the simpler `Core ML + IOSurface` path is sufficient.

#### 3.2.6 ANE peak performance reference points

* **ANE TFLOPS**: A14 = 11 TOPS, A15 = 15.8, A16 = 17, A17 Pro = 35, A18 = 35, A19 (rumoured) ~40+; M1 = 11, M2 = 15.8, M3 = 18, **M4 = 38, M5 = 50–60 TOPS** ([Apple newsroom M5 announcement, October 2025](https://www.apple.com/newsroom/2025/10/apple-unleashes-m5-the-next-big-leap-in-ai-performance-for-apple-silicon/); [hollance/neural-engine supported devices](https://github.com/hollance/neural-engine/blob/master/docs/supported-devices.md); [maderix M4 ANE part 2](https://maderix.substack.com/p/inside-the-m4-apple-neural-engine-615)).
* **Working-set sweet spot**. The M4 ANE has on-die SRAM such that 24 MB working sets fit (≈ 2048×2048 FP16 single-channel tile) and run near peak; 96 MB working sets spill to DRAM and lose ~30 % throughput. cpipe's full-resolution AI ISP nodes will not fit even in M4 ANE SRAM at 100 MP; tile size 2048×2048 is a target.
* **Idle power** = exactly 0 mW (hard power gating on M4); active around 2.8 W at peak.

#### 3.2.7 ONNX Runtime CoreML EP details

The CoreML EP wraps Core ML, taking an `.onnx` model, converting partition-by-partition to MIL on the fly, and dispatching. EP options expose:
* **`coreml_model_format`** = `MLProgram` (default in 1.25) | `NeuralNetwork`.
* **`coreml_compute_units`** = `CPUAndNeuralEngine` | `CPUAndGPU` | `CPUOnly` | `ALL`.
* **`require_static_input_shapes`** = bool — ANE requires static shape; if your input shape is dynamic, you fall back to GPU/CPU.

The EP binds inputs through ORT's IOBinding interface; the underlying type is `MLMultiArray`. Unlike QNN's HTP shared-memory case, the IOBinding for CoreML is genuinely zero-copy *if* the Multi-Array is constructed from an IOSurface-backed CVPixelBuffer the caller built. cpipe's `BufferAdapter` must build that pixel buffer; otherwise ORT fakes it through `MLMultiArray` over a host pointer, which copies.

### 3.3 Cross-API Synchronisation

cpipe DAG edges that cross GPU → NPU or NPU → GPU need a sync primitive. Per platform:

#### 3.3.1 Android: Vulkan + HTP

Direct fence-on-fence is **not** supported between Vulkan and HTP. The reasons are architectural: HTP completion arrives via the CDSP RPC mechanism through a kernel callback, not through a binder-level fence file descriptor. The `VK_KHR_external_semaphore` / `VK_KHR_external_fence` extensions support `OPAQUE_FD` and `SYNC_FD`, neither of which the QNN runtime exports.

The pragmatic pattern:

```cpp
// Pseudocode — cpipe scheduler edge GPU → HTP
// 1. End the Vulkan command buffer and signal a timeline semaphore.
VkSubmitInfo si = {};
VkTimelineSemaphoreSubmitInfo tsi = {};
tsi.signalSemaphoreValueCount = 1;
tsi.pSignalSemaphoreValues    = &(uint64_t){gpu_done_value};
si.pNext                      = &tsi;
si.signalSemaphoreCount       = 1;
si.pSignalSemaphores          = &gpu_done;
vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);

// 2. Wait for it CPU-side (timeline; cheap if no contention).
VkSemaphoreWaitInfo wi = { /* gpu_done at gpu_done_value */ };
vkWaitSemaphoresKHR(device, &wi, UINT64_MAX);

// 3. Now safe to invoke HTP.
qnn_execute_async(graph, inputs, outputs, on_complete_cb);

// 4. on_complete_cb signals the next stage, e.g. another VkSemaphore.
```

The CPU thread that does the wait is a worker thread of the heterogeneous scheduler ([03](03-heterogeneous-scheduler.md)); it is not the application's main thread. The wait is short — sub-millisecond when the Vulkan dispatch is well sized — and cheaper than the 30 ms memcpy AHB → rpcmem that follows when the path is "real" zero-copy.

#### 3.3.2 Apple: Metal + ANE

The cleaner story. Once an IOSurface is shared between Metal and Core ML, the system framework guarantees **read-after-write ordering when accessed via the same dispatch queue**. For cross-queue or cross-process the right primitive is `MTLSharedEvent`:

```swift
// Pseudocode — cpipe scheduler edge GPU → ANE
let sharedEvent = device.makeSharedEvent()!
var value: UInt64 = 1

// 1. Encode GPU work; signal sharedEvent at value=1.
let gpuCmd = queue.makeCommandBuffer()!
// ... encode passes that write into the shared IOSurface ...
gpuCmd.encodeSignalEvent(sharedEvent, value: value)
gpuCmd.commit()

// 2. ANE work waits for the same value.
sharedEvent.notify(listener, atValue: value) { _, _ in
    let pred = try model.prediction(input: input) // ANE
    completion(pred)
}
```

For Metal 4 with `MTLStageMachineLearning`, the sync is *internal* to the command buffer — even simpler.

#### 3.3.3 Vendor-private kernel-side fences

Both Qualcomm and Apple have private kernel paths between their GPUs and NPUs that exchange fence file descriptors at kernel level. They are not exposed to user space. Drawthings [engineering blog](https://engineering.drawthings.ai/p/making-apple-neural-engine-work-in) discusses the ANE work-completion model in detail; the conclusion mirrors the recommendation above (use `MTLSharedEvent` for user-space sync).

### 3.4 Cross-platform Abstraction: `InferenceContext` + `BufferAdapter`

The plugin-facing API surface is the `InferenceSession` introduced in [04](04-mobile-ai-inference.md) §4. The implementation hides Hexagon vs ANE behind two helpers:

**`cpipe::InferenceContext`** — singleton per process, owns:
* The set of available NPU devices (`{kHexagonHtp, kAppleAne, kCpu, kVulkan}`).
* The QAIRT `libQnnHtp.so` handle (Android) or the Core ML framework (Apple).
* The HTP context-binary cache directory (Android).
* The `BufferAdapter` factory.

**`cpipe::BufferAdapter`** — a thin per-platform adapter:

```cpp
class BufferAdapter {
public:
    virtual ~BufferAdapter() = default;

    // Convert a unified Buffer to a backend-acceptable handle.
    // Backend = the chosen execution path for this node.
    struct AdaptedHandle {
        void*       ptr;          // host-readable if backend != HTP
        Buffer*     buf;           // origin
        std::any    backend_token; // VkBuffer, MLMultiArray, rpcmem ptr...
    };

    virtual Status Adapt(Buffer* in, Backend backend,
                         AdaptedHandle* out) = 0;
};

class AndroidBufferAdapter : public BufferAdapter {
    // AHB → VkBuffer (Vulkan)
    // AHB → rpcmem (HTP)  [memcpy]
    // host → rpcmem (HTP) [memcpy if not already rpcmem]
};

class AppleBufferAdapter : public BufferAdapter {
    // MTLBuffer / MTLTexture → CVPixelBuffer over IOSurface
    // CVPixelBuffer → MLMultiArray (zero-copy if format & layout match)
};
```

**Precision handling per D9.** Each `InferenceSession` exposes `manifest()` returning the per-input/output dtype (FP16, FP32, INT8, UINT8). The scheduler ([03](03-heterogeneous-scheduler.md)) reads upstream node output dtype, downstream node input dtype, and inserts conversion only when they differ. The conversion node is itself a tiny GPU shader (FP32→FP16) or a CPU loop (UINT8→FP16); the latency is microseconds and is amortised across batch frames.

### 3.5 Buffer flow diagrams

#### 3.5.1 Android, AI denoise with HTP

```
┌────────────────────┐
│ Camera2 RAW        │
│ AImageReader       │
│ → AHardwareBuffer  │
└─────────┬──────────┘
          │
          │ VkBuffer import (zero copy)
          ▼
┌─────────────────────┐    GPU pre-process (linearize, demosaic stub)
│ Vulkan compute      │────────────────────────────────────────────┐
│ VkBuffer (FP16)     │                                            │
└─────────┬───────────┘                                            │
          │ memcpy required                                        │
          │ (AHB → rpcmem; ~165 MB at 100MP)                        │
          ▼                                                        │
┌─────────────────────┐                                            │
│ rpcmem buffer       │                                            │
│ (HTP shared memory) │                                            │
└─────────┬───────────┘                                            │
          │ QNN graph register / execute                           │
          ▼                                                        │
┌─────────────────────┐    HTP runtime libQnnHtp.so                │
│ HTP execution       │                                            │
│ (V79 INT8 / A16W8)  │                                            │
└─────────┬───────────┘                                            │
          │                                                        │
          ▼                                                        │
┌─────────────────────┐                                            │
│ rpcmem output       │                                            │
└─────────┬───────────┘                                            │
          │ memcpy back to VkBuffer for downstream GPU node        │
          ▼                                                        │
┌─────────────────────┐                                            │
│ Vulkan compute      │ ◀──────────────────────────────────────────┘
│ tone, sharpen,      │
│ HEIF encode        │
└─────────────────────┘
```

#### 3.5.2 Apple, AI denoise with ANE

```
┌────────────────────┐
│ AVCaptureSession   │
│ → CVPixelBuffer    │
│ (IOSurface-backed) │
└─────────┬──────────┘
          │ MLMultiArray.init(pixelBuffer:) — zero copy
          ▼
┌─────────────────────┐
│ Core ML model       │ MLComputeUnits: cpuAndNeuralEngine
│ → ANE FP16          │
└─────────┬───────────┘
          │ output is a new MLMultiArray over a different IOSurface
          ▼
┌─────────────────────┐ Same IOSurface bound as MTLTexture
│ Metal compute       │
│ tone, sharpen,      │
│ HEIF encode         │
└─────────────────────┘
```

The Apple path saves the AHB → rpcmem memcpy and the rpcmem → VkBuffer memcpy on each end of the NPU node — at 100 MP this is ~60 ms saved per frame. The asymmetry (Apple is faster zero-copy than Android) is real and is why some camera apps ship Apple-only AI features that Android skips.

### 3.6 MediaTek APU + Samsung NPU (light coverage per D13)

#### 3.6.1 MediaTek APU

MediaTek Dimensity SoCs (8200, 9200, 9300, 9400) ship the MDLA (Deep Learning Accelerator) inside the APU (AI Processing Unit) cluster. SDK is **NeuroPilot**, with **NeuroPilot Express SDK** as the slimmer, ExecuTorch-aligned variant. Format on disk is `.dla` (MediaTek's container) plus a graph descriptor; the runtime loads through `libneuron`. Quantisation precisions: A16W16, A16W8, A16W4, A8W8, A8W4. License: MediaTek proprietary (the runtime); **Samsung's ONE project at github.com/Samsung/ONE is Apache-2.0 and unrelated to MediaTek**. ExecuTorch ships a MediaTek backend partitioner that targets the NeuroPilot Express SDK; this is the cpipe path if MediaTek support is added later.

#### 3.6.2 Samsung NPU

Samsung Exynos 2400 and 2600 ship Samsung-designed NPUs. SDKs:
* **Exynos AI Studio** — Samsung's authoring tool (proprietary).
* **ENN SDK** (Exynos Neural Network) — runtime; format `.nnc`; proprietary.
* **ONE (On-device Neural Engine)** — open-source library at <https://github.com/Samsung/ONE>, Apache-2.0, that supports CPU, GPU, DSP, NPU. Supports TFLite and ONNX imports; coverage of Exynos NPU varies by model.

cpipe v1 does not target Samsung Exynos NPU. If pursued post-v1, the path is ONE for the Apache-2.0 stack alignment, accepting that operator coverage and Samsung-proprietary feature parity will be incomplete.

---

## 4. Architecture sketches

### 4.1 `cpipe::InferenceContext` initialisation

```cpp
namespace cpipe {

class InferenceContext {
public:
    static InferenceContext& Instance();

    // Discover available devices at startup.
    // Returns the most-preferred device first.
    std::vector<Backend> DiscoveredBackends() const;

    // Get an adapter capable of converting Buffer between platforms.
    BufferAdapter& Adapter() const;

    // QAIRT context-binary cache directory (Android only).
    std::filesystem::path HtpCacheDir() const;

private:
    InferenceContext();    // dlopen libQnnHtp.so on Android;
                           // verify Core ML on Apple;
                           // probe Vulkan device.

    std::vector<Backend>           backends_;
    std::unique_ptr<BufferAdapter> adapter_;
    std::filesystem::path          htp_cache_dir_;
};

} // namespace cpipe
```

### 4.2 Per-edge selection algorithm (heuristic)

Given an upstream node output buffer and a downstream node manifest, the scheduler picks the best edge type:

```
upstream device | downstream device | edge action
================|===================|=============================
GPU (Vulkan)    | HTP               | memcpy VkBuffer → rpcmem
GPU (Vulkan)    | Vulkan            | aliased VkBuffer (zero copy)
GPU (Metal)     | ANE               | wrap IOSurface as MLMultiArray (zero copy)
GPU (Metal)     | Metal             | aliased IOSurface (zero copy)
HTP             | GPU               | memcpy rpcmem → VkBuffer
ANE             | GPU               | rebind same IOSurface (zero copy)
CPU             | HTP               | direct rpcmem allocate (zero copy if upfront)
CPU             | ANE               | wrap as IOSurface-backed CVPixelBuffer (zero copy if upfront)
HTP             | HTP               | rebind rpcmem (zero copy)
ANE             | ANE               | rebind IOSurface (zero copy)
```

The "memcpy" entries are the cost of doing business in 2026 on Android. They are predictable, parallelisable (the scheduler can overlap memcpy with the next GPU dispatch), and recover-able if AHB-direct-DMA lands in a future QAIRT.

### 4.3 Sync token type

```cpp
namespace cpipe {

class SyncPoint {
public:
    enum class Kind { kVkSemaphore, kMtlEvent, kHostFuture };

    static SyncPoint MakeFromVk(VkSemaphore sem, uint64_t value);
    static SyncPoint MakeFromMtl(id<MTLSharedEvent> ev, uint64_t value);
    static SyncPoint MakeFromFuture(std::future<void>);

    Kind     kind() const noexcept;
    Status   Wait(Duration timeout) const;
    bool     Ready() const noexcept;
};

} // namespace cpipe
```

The scheduler chains `SyncPoint`s end-to-end. HTP completions arrive as host futures (HTP completion is a CPU-side callback); Vulkan completions are timeline semaphores; Metal completions are shared events. All collapse to the same `Wait()` interface.

---

## 5. Cited Sources

* Qualcomm AI Engine Direct (QNN) ExecuTorch backend ─ <https://docs.pytorch.org/executorch/stable/backends-qualcomm.html> ─ accessed 2026-05-08.
* QAIRT Setup ─ <https://docs.qualcomm.com/nav/home/general_setup.html?product=1601111740009302> ─ accessed 2026-05-08.
* QAIRT HTP backend ─ <https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-10/htp_backend.html> ─ accessed 2026-05-08.
* Qualcomm AI Hub release notes ─ <https://workbench.aihub.qualcomm.com/docs/hub/release_notes.html> ─ QAIRT 2.43 accessed 2026-05-08.
* QNN HTP shared buffer tutorial ─ <https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-50/htp_shared_buffer_tutorial.html> ─ accessed 2026-05-08.
* ONNX Runtime QNN EP docs ─ <https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html> ─ accessed 2026-05-08.
* ORT QNN HTP shared memory PR #23136 ─ <https://github.com/microsoft/onnxruntime/pull/23136>.
* ORT QNN HTP allocator fix PR #23892 ─ <https://github.com/microsoft/onnxruntime/pull/23892>.
* ORT NPU mapping PR #24195 ─ <https://github.com/microsoft/onnxruntime/pull/24195>.
* onnxruntime-qnn dedicated repository ─ <https://github.com/onnxruntime/onnxruntime-qnn> ─ accessed 2026-05-08.
* AIMET (BSD-3) ─ <https://github.com/quic/aimet> ─ accessed 2026-05-08.
* Snapdragon 8 Elite product brief ─ <https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Elite-Platform-Product-Brief.pdf>.
* Snapdragon 8 Elite Hexagon Tensor Processor analysis ─ <https://www.emergentmind.com/topics/qualcomm-sm8750-ab-snapdragon-8-elite-hexagon-tensor-processor-htp>.
* Wikipedia Qualcomm Hexagon ─ <https://en.wikipedia.org/wiki/Qualcomm_Hexagon> ─ accessed 2026-05-08.
* Hexagon V73 Programmer's Reference Manual ─ <https://docs.qualcomm.com/bundle/publicresource/80-N2040-53_REV_AB_Qualcomm_Hexagon_V73_Programmers_Reference_Manual.pdf>.
* Hexagon V79 Programmer's Reference Manual ─ <https://docs.qualcomm.com/bundle/publicresource/topics/80-N2040-60>.
* LLVM PR #120983 — V79 support ─ <https://github.com/llvm/llvm-project/pull/120983>.
* Chipsandcheese Hexagon DSP / NPU deep-dive ─ <https://chipsandcheese.com/p/qualcomms-hexagon-dsp-and-now-npu>.
* Qualcomm QNN GPU backend blog (May 2025) ─ <https://www.qualcomm.com/developer/blog/2025/05/unlocking-power-of-qualcomm-qnn-execution-provider-gpu-backend-onnx-runtime>.
* Mobile NPU LLM scaling paper ─ <https://arxiv.org/html/2509.23324v1> ─ 2025.
* AI Hub model zoo releases ─ <https://github.com/qualcomm/ai-hub-models/releases>.
* VK_QCOM_image_processing extension ─ <https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html>.
* VK_ANDROID_external_memory_android_hardware_buffer ─ <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_ANDROID_external_memory_android_hardware_buffer.html>.
* Vulkan timeline semaphore reference ─ <https://docs.vulkan.org/samples/latest/samples/extensions/timeline_semaphore/README.html>.
* Apple Core ML overview ─ <https://developer.apple.com/documentation/coreml> ─ accessed 2026-05-08.
* Apple coremltools — ML Programs vs Neural Networks ─ <https://apple.github.io/coremltools/docs-guides/source/comparing-ml-programs-and-neural-networks.html>.
* Apple coremltools — Typed Execution ─ <https://apple.github.io/coremltools/docs-guides/source/typed-execution.html>.
* Apple Newsroom M5 announcement ─ <https://www.apple.com/newsroom/2025/10/apple-unleashes-m5-the-next-big-leap-in-ai-performance-for-apple-silicon/> ─ October 2025.
* Apple WWDC22 ML digital lounge — IOSurface for ANE ─ <https://yono.ai/articles/wwdc22-machine-learning-digital-lounge/question053a/>.
* Apple Wiki Neural Engine ─ <https://apple.fandom.com/wiki/Neural_Engine>.
* Orion: Characterizing and Programming Apple's Neural Engine ─ <https://arxiv.org/abs/2603.06728>.
* maderix — Inside the M4 Apple Neural Engine, Part 1 ─ <https://maderix.substack.com/p/inside-the-m4-apple-neural-engine>.
* maderix — ANE part 2 (benchmarks) ─ <https://maderix.substack.com/p/inside-the-m4-apple-neural-engine-615>.
* maderix — ANE part 3 (training) ─ <https://maderix.substack.com/p/inside-the-m4-apple-neural-engine-c8b>.
* hollance/neural-engine ─ <https://github.com/hollance/neural-engine> ─ accessed 2026-05-08.
* hollance/neural-engine supported devices ─ <https://github.com/hollance/neural-engine/blob/master/docs/supported-devices.md>.
* skyfallsin Apple Neural Engine field guide ─ <https://github.com/skyfallsin/apple-neural-engine-field-guide>.
* DrawThings — Making Apple Neural Engine work in a custom inference stack ─ <https://engineering.drawthings.ai/p/making-apple-neural-engine-work-in>.
* Apple WWDC25 — Discover Metal 4 ─ <https://developer.apple.com/videos/play/wwdc2025/205/>.
* Apple Metal MTLEvent / MTLSharedEvent ─ <https://developer.apple.com/documentation/metal/mtlevent>; <https://developer.apple.com/documentation/metal/mtlsharedevent>.
* Apple WWDC24 HDR for dynamic image experiences ─ <https://developer.apple.com/videos/play/wwdc2024/10177/>.
* MediaTek NeuroPilot Express ExecuTorch backend ─ <https://docs.pytorch.org/executorch/stable/backends-mediatek.html>.
* Samsung ONE on-device neural engine ─ <https://github.com/Samsung/ONE>.
* Samsung Exynos AI Studio overview ─ <https://semiconductor.samsung.com/news-events/tech-blog/unpacking-samsungs-comprehensive-on-device-ai-sdk-toolchain-strategy/>.
* Lightricks Tech Blog — IOSurface and zero-copy in iOS ─ <https://medium.com/lightricks-tech-blog/efficient-image-processing-in-ios-part-2-a96f0343e6f0>.
* Apple WWDC21 — Image processing with Apple silicon ─ <https://developer.apple.com/videos/play/wwdc2021/10153/>.

---

## 6. See also

* [02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md) — owns the platform-agnostic `Buffer`. This report's `BufferAdapter` is the per-platform realisation.
* [03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md) — owns the cross-device topological-sort and the conversion-op insertion described in §4.2.
* [04 — Mobile AI Inference Engines](04-mobile-ai-inference.md) — pairs with this report; the engine-level selection sits above the NPU-level details here.
* [08 — AI ISP Algorithms](08-ai-isp-algorithms.md) — drives the model size and shape constraints assumed in the latency table §3.1.6 and the SRAM working-set discussion §3.2.6.
* [10 — Plugin Architecture](10-plugin-architecture.md) — ratifies the plugin-facing `InferenceSession` API; v2 C-ABI must export `BufferAdapter` capabilities so a plugin's own buffers can interop.
* [16 — Camera2 RAW and Burst](16-camera2-raw-and-burst.md) — the AImageReader → AHardwareBuffer producer side of §3.5.1.

---

## 7. Open Questions

1. **AHB → HTP zero-copy.** Track [PR #24195](https://github.com/microsoft/onnxruntime/pull/24195) and the QAIRT 2.43+ release notes. If a vendor-private extension lands that lets HTP DMA AHardwareBuffer pages directly, the §3.5.1 path loses the input + output memcpy (~60 ms saved at 100 MP). Until then, accept the cost.
2. **HTP context-binary cache key.** Today we key by `(model_hash, soc_id, qairt_version)`. SoC IDs are `SM8650`, `SM8750`, `SM8850`. We need to confirm this list is exhaustive for v1 launch geographies; if some Snapdragon variants share an HTP version but report different SoC IDs we may compile redundantly. Action: enumerate `soc_id → htp_version` from QAIRT 2.43 docs and short-circuit the cache when only `htp_version` matters.
3. **MLProgram on older Apple devices.** `MLProgram` requires iOS 15 / macOS 12. cpipe v2 minimum target is iOS 17 / macOS 14, so we are fine — confirm the deployment matrix once Apple support is on the timeline.
4. **ANE compute precision and AI demosaic accuracy.** The ANE only runs FP16. Some demosaic models (DemosaicNet variants) trained in FP32 lose visible quality when constrained to FP16 — needs in-house empirical comparison against a DNG corpus before locking the precision policy on Apple. Track in [08](08-ai-isp-algorithms.md).
5. **Tile boundaries on 100 MP inputs.** D2 sets 100 MP. Both the HTP (working set fits in DDR but compile times balloon at large shapes) and the ANE (24 MB SRAM sweet spot at 2048²) require tiling. Tile size 2048 with 64-pixel halos is a starting point; need empirical measurement of seam artifacts on real cpipe AI denoise output.
6. **MTLStageMachineLearning timeline (Metal 4).** Apple's WWDC25 announcement includes ML encoder synced with rendering. Promising for v3, irrelevant for v2 launch. Document but don't depend on it for v2.
7. **MediaTek / Samsung future scope.** D13 says "mentioned but not deeply researched" — re-evaluate after v1 if Android device matrix demands it. ExecuTorch's MediaTek backend is real today; the Samsung path is via Apache-2.0 ONE if we want to stay licence-clean.
8. **QNN GPU backend evaluation.** When the input is a Vulkan buffer that has not been routed through rpcmem (e.g., output of an upstream GPU node), the QNN GPU backend may beat the HTP because it skips the memcpy. Worth a benchmark on Adreno 750 (8 Gen 3) for the AI denoise model at 1080p tile.
9. **Cross-process zero-copy.** v1 is a single-process CLI / app. If v2's plugin ABI ever supports out-of-process plugins (per D4 v2 plans), we must extend the buffer adapter to handle cross-process IOSurface (works on Apple) and AHardwareBuffer (works on Android via socket transfer); rpcmem cross-process is not standard and would break.
10. **Apple Adaptive HDR end-to-end.** D7 demands UltraHDR + Apple Adaptive HDR. The gain map can be either authored or model-inferred. Where in the AI ISP pipeline does the gain map come from? If from a model, it is one more node consuming an IOSurface and producing another; if computed deterministically, it is a Metal kernel. Decision lives in [14 — HEIF and HDR Output](14-heif-and-hdr-output.md) but cpipe should plan the AI HDR node to produce the gain map alongside the SDR base image so the path is one IOSurface in, two IOSurfaces out.
