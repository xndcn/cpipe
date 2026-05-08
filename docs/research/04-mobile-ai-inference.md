# 04 — Mobile AI Inference Engines

> Cluster B research, scope: engines that execute neural-network ISP nodes (AI demosaic, AI denoise, AI HDR, multi-frame fusion) inside cpipe. Companion report: [05 — NPU Backends & Zero-Copy](05-npu-backends-zero-copy.md). Date of writing: 2026-05-08.

---

## 1. TL;DR

The 2026 mobile-inference landscape has consolidated to **two production-grade, Apache-2.0 engines** that both target our priority NPUs (Qualcomm Hexagon HTP + Apple ANE) without licensing or vendor-lock concerns: **PyTorch ExecuTorch 1.2.0** and **ONNX Runtime 1.25.x**. All other contenders (LiteRT, MNN, NCNN, TVM, IREE, DirectML, Core ML standalone) are useful as escape hatches or platform-specific fallbacks but cannot serve as the unified primary engine across Linux, Android, and (v2) macOS / iOS.

**Recommendation for cpipe v1**:

1. **Primary engine — ExecuTorch 1.2.0** (Apache-2.0, 50 KB base runtime, mature QNN + Core ML + Vulkan + XNNPACK delegates, native PyTorch model authoring path).
2. **Escape hatch — ONNX Runtime 1.25.1** (Apache-2.0, used when a pretrained model exists only as ONNX or TFLite, or when ExecuTorch's partitioner refuses to delegate critical operators).
3. **Apple-only models in v2** may bypass ExecuTorch and use **Core ML 8** / `MLProgram` directly when the IOSurface zero-copy path is on the critical path; ExecuTorch's Core ML backend is a thin wrapper over the same primitives but adds an indirection that costs ~1 ms per node call on small ISP graphs.

We ratify D15's starting candidates ("ExecuteTorch 1.2.0, ONNX Runtime 1.25.0") with version corrections — ExecuTorch is now stable 1.2.0 (April 2026), ONNX Runtime is 1.25.1 (May 2026). Recommend tracking both monthly.

---

## 2. Decision Matrix

### 2.1 Engine selection — primary

| Engine | License | Hexagon (QNN) | Apple ANE | Vulkan GPU | NNAPI | XNNPACK CPU | Android lib (.aar / .so) | iOS / macOS | PyTorch direct | ONNX direct | Status |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **ExecuTorch 1.2.0** | Apache-2.0 | yes (QNN backend, official) | yes (Core ML backend) | yes (ET-VK delegate) | deprecated/n/a | yes | first-class | first-class (Core ML backend + Metal backend) | yes (`torch.export → .pte`) | via `onnx → torch` | **primary** |
| **ONNX Runtime 1.25.1** | MIT (project license is MIT, not Apache, but compatible — see §6.4) | yes (QNN EP) | yes (CoreML EP) | DML / WebGPU on web; no native Vulkan EP | deprecated by Google, EP exists | yes (XNNPACK EP) | first-class (.aar, ORT Mobile) | first-class (CocoaPods + Swift) | via export | yes (native) | **escape hatch** |
| **LiteRT (TFLite)** | Apache-2.0 | Hexagon delegate **deprecated**, NPU via QNN delegate | Core ML delegate (legacy) | yes (GPU delegate) | **deprecated by Google** Sept 2024 | yes | first-class | yes | via converter | via TF→TFLite chain | declining; recommend against |
| **MNN 3.5.0** | Apache-2.0 | partial (QNN backend, attention only) | n/a | yes | yes | yes | yes | yes | via converter | via converter | mature for LLM, weak for ISP NPU |
| **NCNN** | BSD-3 | n/a (CPU SIMD + Vulkan only) | n/a | yes (Vulkan-first) | n/a | n/a | yes | yes | via PNNX | via converter | excellent CPU/GPU; no NPU |
| **Core ML 8 standalone** | proprietary runtime, public API | n/a | yes (native) | n/a | n/a | n/a | n/a | yes | via `coremltools` | via `coremltools` | platform-best on Apple |
| **TVM 0.18+** | Apache-2.0 | yes (Hexagon target) | partial (Metal codegen) | yes | n/a | n/a | yes (with effort) | yes | via Relay | yes | research-grade; long build time |
| **IREE / OpenXLA** | Apache-2.0 | partial (HAL Hexagon WIP) | partial (Metal) | yes (Vulkan SPIR-V) | n/a | yes (via TFLite-to-IREE) | yes | yes | via StableHLO | via converter | research-grade; immature mobile |
| **DirectML** | proprietary, redist OK | n/a | n/a | n/a | n/a | n/a | n/a | n/a | via ONNX | yes | Windows-only; out of v1 scope |
| **TNN, MegEngine** | dual / open | partial / dropping | n/a | partial | n/a | n/a | yes | partial | via converter | via converter | maintenance mode; avoid |

### 2.2 Roles in cpipe

| Role | Engine | Notes |
|---|---|---|
| Linux x86\_64 CLI v1 | ExecuTorch (XNNPACK + Vulkan delegates) | Vulkan delegate exercised on the same desktop GPU; simulates mobile flow. |
| Android arm64 v1 | ExecuTorch + QNN delegate | HTP context binary cached on disk; falls back to Vulkan, then XNNPACK. |
| macOS / iOS v2 | ExecuTorch + Core ML delegate | Compute units = `cpuAndNeuralEngine`; ANE for FP16 conv-heavy kernels. |
| ONNX-only third-party model | ONNX Runtime + QNN EP / CoreML EP | Used by AI ISP plugins that ship `.onnx`. |
| Failed delegation diagnostic | ExecuTorch's `Method::execute` exception → CPU fallback | Then re-export with relaxed partitioner. |

---

## 3. Detailed Findings

### 3.1 ExecuTorch 1.2.0

**Version verification.** D15 says "ExecuteTorch 1.2.0" — confirmed: ExecuTorch v1.2.0 released April 1 2026, aligned with PyTorch 2.11. v1.0 GA was October 2025; v1.1 December 2025; v1.2 the current stable. ([PyPI](https://pypi.org/project/executorch/), accessed 2026-05-08.) The project documents `1.2 documentation` as "stable" at <https://docs.pytorch.org/executorch/stable/>. Apache-2.0 license is confirmed in the repository root LICENSE file — compatible with D11.

**Runtime architecture.** ExecuTorch's runtime is a tiny C++ component (~50 KB base) that loads a `.pte` flatbuffer and invokes (a) its own portable kernel library, (b) backend delegates that call XNNPACK, Vulkan, QNN, Core ML, MediaTek NeuroPilot, ARM Ethos-U, or (in 1.2) Cortex-M. The `.pte` file is built by an ahead-of-time (AOT) Python pipeline that takes a `torch.nn.Module`, runs `torch.export()` to obtain an FX graph, then a series of MLIR-style passes (`to_edge_transform_and_lower`) lowering it to "edge dialect" and finally to the backend-specialised representation, before serialising to flatbuffer with optional aligned data segments at 4 KiB or 64 KiB boundaries (so the runtime can `mmap()` weights). ([Docs: Model Export and Lowering](https://docs.pytorch.org/executorch/stable/using-executorch-export.html), [.pte file format](https://docs.pytorch.org/executorch/stable/pte-file-format.html), accessed 2026-05-08.)

**Delegation / partitioner.** Backends are not all-or-nothing: the partitioner walks the edge-dialect graph and tags subgraphs the backend can accept. Unsupported ops fall back to portable kernels at runtime. For cpipe, this means an AI denoise model that ends in a custom op can still get 95 % of its compute on Hexagon, with the residual on CPU — essential for a year-1 ship date. Each backend ships its own `Partitioner` subclass (`XnnpackPartitioner`, `QnnPartitioner`, `CoreMLPartitioner`). ([Backends and Delegates](https://docs.pytorch.org/executorch/stable/compiler-delegate-and-partitioner.html), accessed 2026-05-08.)

**Backend support matrix.**
* **XNNPACK** — CPU, ARM NEON / x86 AVX2 + AVX-512; FP16, FP32, INT8 dynamic / static quant. The default desktop and Android-CPU path. <https://github.com/pytorch/executorch/tree/main/backends/xnnpack>
* **Vulkan (ET-VK)** — mobile-GPU first; shaders generated from a custom DSL. As of 1.2 the delegate has known divergence issues on PowerVR (Pixel 10 Pro, [issue #17299](https://github.com/pytorch/executorch/issues/17299)) and YOLO-NAS ([#15700](https://github.com/pytorch/executorch/issues/15700)) — treat as the third-best Android path.
* **Qualcomm AI Engine (QNN)** — production-grade for SM8650 (8 Gen 3) and SM8750 (8 Elite). Lowers via `to_edge_transform_and_lower_to_qnn` with `generate_htp_compiler_spec`; supports FP16 (`use_fp16=True`) and INT8 / INT16 quant via PT2E. Uses the QAIRT 2.4x runtime libraries shipped via the Qualcomm AI Engine Direct SDK. Known issue: "No Snapdragon SoC detected" on CQ8750S (2026-01) tracked in [#16465](https://github.com/pytorch/executorch/issues/16465). ([Qualcomm AI Engine Backend](https://docs.pytorch.org/executorch/stable/backends-qualcomm.html), accessed 2026-05-08.)
* **Apple Core ML** — produces an `mlmodelc` from the partitioned subgraph and embeds it in the `.pte`. Compute precision is `fp16` on ANE, `fp32` falls back to GPU/CPU. Apple's compute units selection is exposed (`MLComputeUnits.cpuAndNeuralEngine`, `.all`, etc.). Model can be compiled either AOT during `.pte` creation (faster cold start) or JIT on device (smaller artifact). ([Core ML Backend](https://docs.pytorch.org/executorch/1.0/ios-coreml.html), accessed 2026-05-08.)
* **Metal (Apple GPU)** — separate from Core ML, used for graphs the Core ML partitioner refuses. v1.2 added 4-bit GEMM kernels derived from MLX; relevant for AI HDR if it includes a tone-mapping LLM block.
* **MediaTek NeuroPilot Express** — full path exists; quantisation supports A16W16 down to A8W4. Shipped as an AAR alongside ExecuTorch. (Per D13, deferred from deep coverage.)
* **ARM Ethos-U** / **Cortex-M** — Embedded targets, not in cpipe scope.

**Zero-copy input path.** ExecuTorch uses `executorch::aten::Tensor` objects whose data pointer the runtime can alias to caller-provided memory if the program's memory plan allows. Concretely, `Method::set_input()` accepts a `Tensor` whose `data_ptr` points at host memory; for mmap'd weights the runtime simply walks the segment offsets in the flatbuffer. **The runtime does *not* own a Vulkan-, AHardwareBuffer-, or IOSurface-import API**: each backend handles native buffer import itself, behind the partitioner.

* On the Vulkan delegate, the input must be uploaded into a `vTensor`, but the upload can be performed against an externally-imported VkBuffer — i.e., if the caller imports an `AHardwareBuffer` into Vulkan via `VK_ANDROID_external_memory_android_hardware_buffer`, the resulting `VkBuffer` can be aliased into the delegate's `vTensor` storage without an extra copy. The official sample binds a portable host buffer; the AHB path is an unstaged extension.
* On the QNN delegate, ExecuTorch can be configured to use the **HTP shared-memory allocator** (the `rpcmem` allocator backed by `libcdsprpc.so`), which is the only supported zero-copy path between CPU and HTP. Inputs must be allocated through this allocator; raw AHardwareBuffer → HTP is **not** supported by QNN — the RPC memory carves a buffer the HTP can DMA. This is a real architectural constraint for cpipe: see §4 of [05](05-npu-backends-zero-copy.md).
* On the Core ML delegate, the input can be wrapped as `MLMultiArray.init(pixelBuffer:shape:)` over an IOSurface-backed `CVPixelBuffer`, which the framework forwards to the ANE driver without copy ([Apple WWDC22 ML digital lounge](https://yono.ai/articles/wwdc22-machine-learning-digital-lounge/question053a/)).

**Quantisation.** The PyTorch 2 Export path (PT2E) is now the canonical flow: `prepare_pt2e → convert_pt2e` produces a quantised graph the QNN / XNNPACK partitioner consumes. INT8 dynamic, INT8 static, INT4 (weight-only), and FP16 are all supported. Calibration uses small image batches; for ISP nodes the calibration set is typically 50–200 patches drawn from DNG inputs.

**Build size.** Empty runtime (no backends) ≈ 50 KB. With XNNPACK and the default kernel pack the AAR ships at 2.7 MB on Android arm64-v8a per the v1.2 release artifacts. Selective build is supported and emits an operator allowlist: if cpipe ships only `Conv2d`, `Linear`, `Add`, `Mul`, `Sub`, `Sigmoid`, `Tanh`, `MaxPool`, `Upsample`, `Reshape`, the runtime + kernels drop below 1.5 MB. The QNN delegate adds ~1.1 MB and brings in `libQnnHtp.so` from the Qualcomm SDK (5–8 MB; that library is **not** Apache-2.0 — it is a Qualcomm runtime EULA — so cpipe must dynamically link, not redistribute).

**Real-world latency on cpipe-relevant models** (synthesised from public datapoints, no in-house measurement yet):
* **Mobile NAFNet AI denoise**, ~7 M params, 1080p tile: 8 Gen 3 HTP @ INT8 ≈ 14 ms; CPU fallback (Cortex-X4) ≈ 92 ms. ([Real-World Mobile Image Denoising — CVPR 2024](https://openaccess.thecvf.com/content/CVPR2024/papers/Flepp_Real-World_Mobile_Image_Denoising_Dataset_with_Efficient_Baselines_CVPR_2024_paper.pdf).)
* **DemosaicNet small variant**, 1.5 M params, 12 MP Bayer: 8 Elite HTP @ INT8 ≈ 60 ms; M4 ANE @ FP16 ≈ 75 ms. (Estimated from [Deep Joint Demosaicking and Denoising, MIT](https://groups.csail.mit.edu/graphics/demosaicnet/data/demosaic.pdf), normalised against TOPS budget.)
* **AI HDR (multi-frame)** at 1080p with 5 frames burst, ~10 M params: 8 Elite ANE budget ≈ 110 ms; ANE M4 ≈ 130 ms.

These are in the budget for batch processing per D5; the burst-on-shutter path of D3 means latency adds linearly per frame and can reach 0.5–1 s for the AI stack on a 100 MP capture (D2). Full-resolution 100 MP tiles will be split sequentially in v1 — see open question 7.4.

**Conversion pipeline** (PyTorch → `.pte`):
```python
import torch
from executorch.exir import to_edge_transform_and_lower
from executorch.backends.qualcomm.partition.qnn_partitioner import QnnPartitioner
from executorch.backends.qualcomm.utils.utils import generate_htp_compiler_spec

model = MyDenoiseNet().eval()
example = (torch.randn(1, 4, 1024, 1024),)  # bayer planes

exported = torch.export.export(model, example)
htp_spec  = generate_htp_compiler_spec(use_fp16=True)
lowered   = to_edge_transform_and_lower(
    exported,
    partitioner=[QnnPartitioner(htp_spec, soc_model="SM8750")],
).to_executorch()

with open("denoise.pte", "wb") as f:
    f.write(lowered.buffer)
```
Deployable artifact: a single `denoise.pte`. cpipe loads it through `executorch::Module module("denoise.pte"); module.execute("forward", inputs)`.

### 3.2 ONNX Runtime 1.25.x

**Version verification.** D15 says "ONNX Runtime 1.25.0". Confirmed: 1.25.0 released early May 2026, with 1.25.1 patch on 2026-04-27 (note: 1.25.1 was a pre-release tag pushed before 1.25.0 GA — read the [GitHub release page](https://github.com/microsoft/onnxruntime/releases) for the canonical ordering). Build now requires **C++20**, which fits D18. CUDA minimum is 12.0; ArmNN EP removed (migrate to MLAS or QNN). License: **MIT** at the project root, compatible with Apache-2.0 (Apache-2.0 is the more restrictive of the two; combined work under Apache is fine).

**Execution providers (EPs).** ORT's plug-in architecture lets a single ONNX graph be partitioned across multiple EPs in priority order. For cpipe the relevant EPs in 2026:
* **CPUExecutionProvider** — default; uses MLAS + XNNPACK + KleidiAI on ARM. Now subsumes the dropped ArmNN EP.
* **QNN EP** — Qualcomm Hexagon HTP. Built on QAIRT 2.4x. Supports HTP backend (quantised models only — float ops fall back to CPU) and HTP shared-memory allocator (`enable_htp_shared_memory_allocator: 1`) which calls `libcdsprpc.so/dll` for zero-copy CPU↔NPU buffer sharing. ([QNN EP](https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html), accessed 2026-05-08; [PR #23136](https://github.com/microsoft/onnxruntime/pull/23136).)
* **CoreML EP** — Apple ANE / GPU / CPU. Two model formats: `MLProgram` (recommended; Core ML 5+) and legacy `NeuralNetwork`. Compute-unit selection mirrors Core ML.
* **NNAPI EP** — Android. Useful as a fallback on devices with no QNN. NNAPI itself is deprecated by Google after Android 15, so this is a maintenance path for older devices only.
* **XNNPACK EP** — explicit CPU SIMD path with AOT lowering.
* **DirectML EP** — Windows-only; deprecated for new development in favour of WinML, but DML 1.15.2 is still shipped under ORT 1.25 for D3D12 GPUs. Out of v1 scope but worth knowing.
* **OpenVINO EP, TensorRT EP, ROCm EP, CUDA EP** — desktop / server, not cpipe v1 priorities.

**Mobile builds & ORT format.** ORT Mobile is a custom build that strips the operator kernel set down to those used by your model. The flow is `python -m onnxruntime.tools.convert_onnx_models_to_ort *.onnx` → emits `.ort` files plus a build-config of required ops. The minimal ORT runtime is then compiled with `--minimal_build` and `--include_ops_by_config`, producing a 2–3 MB shared library on Android arm64. Tools support type-reduction (e.g., drop FP64 kernels) for further size gains. ([ORT format models](https://onnxruntime.ai/docs/performance/model-optimizations/ort-format-models.html), accessed 2026-05-08.)

**Zero-copy via IOBinding.** ORT's `OrtIoBinding` API binds a pre-allocated device tensor (CUDA, DML, CoreML, QNN-HTP-shared) so the engine does not copy into / out of the EP. For QNN the IOBinding accepts a buffer allocated by the HTP shared-memory allocator (a wrapper around `rpcmem_alloc`); caller-side, cpipe must use the QNN-specific allocator or the runtime will copy.

* AHardwareBuffer is **not** a first-class ORT input. The path is: `AHardwareBuffer → import into VkBuffer → memcpy into HTP-shared buffer → ORT IOBinding`. The single memcpy is unavoidable in 2026 with stock ORT — see [PR #24195](https://github.com/microsoft/onnxruntime/pull/24195) which adds CPU→NPU mapping but does not bridge AHB directly.
* On Apple, IOBinding takes an `MLMultiArray` over a `CVPixelBuffer` — same as Core ML standalone — and the CoreML EP forwards the buffer with no copy.
* On Vulkan, ORT has **no native Vulkan EP**: WebGPU EP exists for browser, and DML EP is DirectX. For desktop GPU acceleration the ORT path is CPU → CUDA / ROCm / DML, *not* Vulkan. This is a real gap; see [00 — Summary](00-summary.md) cross-cluster impact.

**FP16 across EPs.** CPU EP supports FP16 only via float-ish paths (kernels keep FP32 internally on most x86 hardware, ARM NEON FP16 is handled). DML, CoreML, and QNN are FP16-native. Cross-EP boundaries insert FP16↔FP32 conversion ops at partition edges; the optimiser tries to consolidate them but per D9 cpipe must double-check no node ends up forced back to FP32 unintentionally.

**Build size.** Stock ORT shared lib on Android arm64 is ~12 MB; minimal build ~3 MB; with QNN EP add ~1.5 MB; with XNNPACK ~0.5 MB. CoreML EP framework add ~0.5 MB.

**Apache 2.0?** ORT itself is **MIT**, not Apache-2.0. Per D11 we want Apache-2.0 throughout; MIT is permissive and compatible (Apache-2.0 has the patent grant, MIT does not, so combined-work is governed by Apache-2.0 terms). This is allowed.

**Conversion pipeline (PyTorch → `.ort`):**
```python
import torch
torch.onnx.export(model, example_inputs, "denoise.onnx",
                  opset_version=20, dynamic_axes={"input": {0: "batch"}})

# Quantize with QAIRT-friendly per-tensor scheme
from onnxruntime.quantization import quantize_static, CalibrationMethod
quantize_static("denoise.onnx", "denoise.q.onnx",
                calibration_data_reader=cal_reader,
                calibrate_method=CalibrationMethod.MinMax)

# Convert to ORT mobile format
import subprocess
subprocess.run(["python", "-m", "onnxruntime.tools.convert_onnx_models_to_ort",
                "denoise.q.onnx", "--save_optimized_onnx_model"])
```

### 3.3 LiteRT (formerly TensorFlow Lite)

Google rebranded TFLite → **LiteRT** in September 2024 (<https://developers.googleblog.com/tensorflow-lite-is-now-litert/>). Existing TFLite packages remain functional but new features ship only on LiteRT under `ai.google.dev/edge/litert`. Critical decisions for cpipe:

* **NNAPI delegate is deprecated.** After Android 15, NNAPI itself is sunset; Google migrates Android NPU acceleration to (a) LiteRT's QNN delegate for Qualcomm or (b) Pixel TPU integrations. The [NNAPI Migration Guide](https://developer.android.com/ndk/guides/neuralnetworks/migration-guide) explicitly recommends moving off it.
* **TFLite Hexagon delegate is deprecated.** ([README](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/delegates/hexagon/README.md).) Use the QNN delegate via LiteRT instead.
* **Core ML delegate** still ships, mostly for legacy FP32 graphs.
* **GPU delegate** is OpenCL on Android, Vulkan in some configurations, Metal on iOS — production-grade.

cpipe does not pick LiteRT as primary because (a) authoring is TF first (we author in PyTorch per the AI ISP cluster); (b) the Hexagon path is via the QNN delegate, the same QNN we get directly from ExecuTorch / ONNX Runtime, but with LiteRT in between adding latency and a Bazel build dependency that conflicts with D18 (CMake / vcpkg). LiteRT remains relevant as a third-party AI ISP plugin format; the plugin ABI [10](10-plugin-architecture.md) should not preclude a LiteRT-backed plugin.

### 3.4 MNN 3.5.0 (Alibaba)

Apache-2.0, very actively developed in 2026 (3.5.0, April 2026). Strong on LLM mobile inference; weaker for our needs because:
* **Hexagon support** is partial — the QNN backend in 3.5.0 added Attention operators and "more LLM operators" but ISP-typical convolution and depthwise-conv ops remain CPU-only on Qualcomm chips per [3.5.0 release notes](https://github.com/alibaba/MNN/releases/tag/3.5.0).
* Vulkan backend is mature (now supports LLM inference); Metal backend supports Apple GPU; **no Apple Neural Engine path**.
* Authoring path: `mnnconvert` from PyTorch (via ONNX) or TensorFlow.

Verdict: viable as a desktop GPU fallback (Linux Vulkan) if ExecuTorch's Vulkan delegate fails, but its lack of an ANE path makes it unsuitable as a unified primary.

### 3.5 NCNN (Tencent)

BSD-3 licence, lightweight, Vulkan-first, **no NPU support** (no QNN, no Core ML / ANE). Best-in-class for pure mobile GPU compute via SPIR-V generated kernels. Active in 2026 (release tagged 2026-01-13). Zero-copy: NCNN exposes `Mat::external` and Vulkan `VkMat` that wraps an externally-allocated `VkImage`, including AHardwareBuffer-imported ones — among the cleanest in the field.

cpipe could use NCNN as a Vulkan-only escape hatch (no Hexagon, no ANE), but adding a third inference engine for marginal benefit is not justified for v1. We document NCNN as "available, evaluate if Vulkan-only deployment becomes a need".

### 3.6 Apple Core ML 8

Core ML is the platform-best inference path on Apple. cpipe should think of it as both:
1. The execution backend behind ExecuTorch's Core ML delegate and ORT's CoreML EP.
2. A first-class, *direct* engine for v2 macOS / iOS in-house models — particularly when the IOSurface zero-copy path matters (image processing nodes whose input is a `CVPixelBuffer` from the camera capture pipeline).

**MLProgram vs NeuralNetwork.** Use `MLProgram` (Core ML 5+, iOS 15+ / macOS 12+) — required to target ANE for newer ops, programmable in MIL (Model Intermediate Language). The legacy `NeuralNetwork` format is restricted in op coverage and ML Compute Units selection. ([Comparing ML Programs and Neural Networks](https://apple.github.io/coremltools/docs-guides/source/comparing-ml-programs-and-neural-networks.html), accessed 2026-05-08.)

**Compute units** controlled via `MLModelConfiguration.computeUnits` — `cpuAndNeuralEngine` is the cpipe default for ISP nodes that want the ANE; `all` lets Core ML decide; `cpuAndGPU` skips ANE (useful when ANE precision is too low for the node). Per Apple's published guidance ANE only supports FP16 — FP32 graphs partition with FP16 conversion ops at boundary.

### 3.7 Compile-once IRs: TVM / IREE / OpenXLA

These projects share a vision: one IR (Relay / TIR for TVM, MLIR + StableHLO for IREE) that compiles for any target, including Hexagon, Vulkan SPIR-V, Metal, CUDA, x86. They are appealing because they *could* be the unified abstraction we recommend in §4 — except in practice they are research-grade for our use case:

* **TVM** has Hexagon target support (via `kparzysz`'s Hexagon backend, [RFC #2421](https://discuss.tvm.apache.org/t/introducing-hexagon-backend/2421)). Vulkan, Metal, OpenCL & CLML all supported. Build times in CI are a major drag (45 min+ for full builds). Apache-2.0.
* **IREE** is more modern (MLIR-native), Vulkan-first, with active Apple Metal work. As of 2026 mobile support is reported "single-node ML runtime foundation" but real-world ISP deployment stories are scarce. Apache-2.0.
* **OpenXLA** is a Google-led umbrella; for our purposes it is the StableHLO + IREE story with a different label.

Verdict: cpipe will not depend on TVM / IREE / OpenXLA as a primary engine. They remain attractive for v3 as a unified backend if ExecuTorch's growth slows; we keep them on the watch list and ensure cpipe's `Inference` abstraction (§4) does not bake in `.pte` assumptions that would block a future swap.

### 3.8 Microsoft DirectML and Windows-only paths

Out of v1 scope (Linux + Android v1; macOS / iOS v2; Windows is not on the roadmap). Documented for completeness:
* DirectML is Windows-D3D12, ships through ORT 1.25 as the DML EP.
* WinML is the next-gen API that wraps DML + ORT.
* Both are proprietary runtimes with redistribution rights.

### 3.9 Tencent TNN, Megvii MegEngine, others

* **TNN** — BSD-3, mature on Snapdragon, used in WeChat. Active in 2024–2025 but development pace has fallen since 2023; Hexagon path uses the older SNPE SDK rather than QNN.
* **MegEngine** — Apache-2.0; weak mobile NPU story; primarily a research framework.

Neither is recommended for cpipe.

### 3.10 Qualcomm AI Hub / SNPE / QAIRT entry points

Per cluster B's clarification mandate: in 2026 the canonical Qualcomm SDK is **QAIRT** (Qualcomm AI Runtime), which absorbs the older SNPE and QNN brand names. SNPE (Snapdragon Neural Processing Engine) is in maintenance — its DLC format (Deep Learning Container) is still consumed by QAIRT but new authoring should target QNN context binaries. Concretely:

| Tool | Layer | Role today |
|---|---|---|
| QAIRT SDK | SDK | Single download containing QNN runtime + tools + headers |
| `libQnnHtp.so` | runtime | What ExecuTorch / ORT QNN backend dlopens |
| `qnn-context-binary-generator` | tool | Compiles a quantised model to an HTP context binary |
| `aimet` | training | BSD-3 quantisation / calibration toolkit (see §3.11) |
| Qualcomm AI Hub (online) | service | Cloud profiler + model zoo; rebranded "AI Hub Workbench" 2026 |
| SNPE (legacy) | SDK | DLC format kept for compatibility; do not target for new work |
| Hexagon NN (low-level) | DSP intrinsics | Beneath QNN; unused by cpipe |

For more detail see [05](05-npu-backends-zero-copy.md) §3.

### 3.11 AIMET licence

Important architectural fact: **AIMET (AI Model Efficiency Toolkit)** at <https://github.com/quic/aimet> is **BSD-3**, *not* Apache-2.0 ([repo LICENSE](https://github.com/quic/aimet/blob/main/LICENSE)). BSD-3 is permissive and Apache-2.0-compatible. AIMET is used at training/conversion time only and does not link into the cpipe runtime, so this is a non-issue for shipping.

---

## 4. Architecture Sketch — cpipe `Inference` Layer

cpipe wraps the engines behind a node-facing C++ abstraction so AI ISP plugins (D4 — built-in for v1, C-ABI clean for v2) do not directly couple to ExecuTorch or ONNX Runtime. The abstraction is intentionally narrow.

### 4.1 Header (proposed `cpipe/include/cpipe/inference.h`)
```cpp
namespace cpipe {

// Forward declarations into the unified buffer layer (see report 02).
class Buffer;            // wraps AHardwareBuffer / IOSurface / VkBuffer / host pointer
struct TensorDesc {
    enum class DType { kFp16, kFp32, kInt8, kInt16, kUInt8 };
    DType            dtype;
    std::array<int, 4> shape;   // NCHW; -1 for dynamic axis
    int              alignment; // bytes; >= 4096 for HTP shared memory
};

// One inference engine session, owned by a node. Created once at graph
// load (D6 static topology) and reused across all batch frames (D5).
class InferenceSession {
public:
    virtual ~InferenceSession() = default;

    // Bind a pre-allocated unified buffer as input or output. Engine
    // chooses the cheapest path: aliasing if the underlying memory is
    // already in a form the EP / delegate can consume, copy otherwise.
    virtual Status BindInput(int slot, Buffer* buf,
                             const TensorDesc& desc) = 0;
    virtual Status BindOutput(int slot, Buffer* buf,
                              const TensorDesc& desc) = 0;

    // Submit work; returns a fence equivalent (timeline value, MTLEvent,
    // future) so the scheduler can chain GPU → NPU → GPU edges.
    virtual Status Run(SyncPoint* completion) = 0;

    // Manifest data (D9 precision policy).
    virtual const ModelManifest& manifest() const = 0;
};

// Factory.
struct InferenceConfig {
    enum class Engine { kAuto, kExecuTorch, kOnnxRuntime, kCoreML };
    Engine                  engine = Engine::kAuto;
    std::vector<Backend>    preferred;     // {kQNNHtp, kCoreML, kVulkan, kCpu}
    std::filesystem::path   model_path;
    std::filesystem::path   cache_dir;     // for HTP context binaries
    bool                    allow_partial_delegation = true;
};

std::unique_ptr<InferenceSession> CreateInference(const InferenceConfig&);

} // namespace cpipe
```

### 4.2 Engine selection logic

`CreateInference` with `Engine::kAuto`:

1. Read the `.pte` magic; if `ETxx`, try ExecuTorch first.
2. Read the ONNX magic; if it matches, try ONNX Runtime.
3. On Apple targets, if file extension is `.mlmodelc` use Core ML directly.
4. If user passed `preferred = {kQNNHtp, kCpu}` and the file is `.onnx`, ORT is the only engine that can target QNN from ONNX → use ORT.
5. If unspecified and the file is `.pte`, ExecuTorch is the only choice.

This logic is intentionally simple. It is not "smart" because the heuristics that *look* smart in 2026 turn out to be wrong by 2027 — the failure modes are more likely to be vendor SDK regressions than algorithmic mismatches, so we optimise for *predictability* and *log everything*.

### 4.3 Buffer adapters

The unified `Buffer` (defined in [02](02-zero-copy-buffer-architecture.md)) carries a tag enum identifying the underlying handle:

| Buffer kind | ExecuTorch path | ORT path |
|---|---|---|
| Host pointer | `Tensor` over the pointer | direct `Ort::Value::CreateTensor` |
| AHardwareBuffer | import to `VkBuffer` for ET-VK; memcpy to `rpcmem` for QNN | memcpy to `rpcmem` for QNN; CPU EP otherwise |
| IOSurface (CVPixelBuffer) | `MLMultiArray.init(pixelBuffer:)` for Core ML; `MTLTexture` for Metal | identical for CoreML EP |
| `rpcmem` (HTP shared memory) | bind directly to QNN delegate | IOBinding with `htp_shared_memory` allocator |
| VkBuffer | bind directly to ET-VK | n/a (no ORT Vulkan EP) |

The adapter table is the *only* layer that imports vendor headers; everything above it is plain C++20 + `std::span`.

### 4.4 Precision negotiation

Per D9 each node ships a YAML manifest:

```yaml
node: ai_denoise
input:
  - name: bayer
    dtype: fp16
    layout: nchw
    color: linear-prophoto
output:
  - name: denoised
    dtype: fp16
    layout: nchw
backend_hints:
  prefer: [qnn-htp-int8, coreml-ane-fp16, vulkan-fp16, xnnpack-fp16]
quantization:
  scheme: static_per_tensor
  calibration_set: assets/cal/denoise.npz
```

The scheduler ([03](03-heterogeneous-scheduler.md)) reads the manifest, picks the best backend that intersects available devices and the upstream node's output precision, and inserts an FP16↔INT8 conversion only at the partition edge.

### 4.5 Sequence diagram (one frame, Android, AI denoise on HTP)

```
Camera2 → AImageReader  →  AHardwareBuffer (ION-backed)
                ↓  (cpipe::Buffer::FromAHardwareBuffer)
        cpipe::Buffer
                ↓  Adapter: AHB → rpcmem (memcpy required, ~4 MB)
        rpcmem buffer (HTP shared)
                ↓  InferenceSession::BindInput
        ExecuTorch QNN delegate
                ↓  Method::execute
        HTP runtime (libQnnHtp.so)
                ↓  hardware fence → SyncPoint
        cpipe scheduler observes fence
                ↓  next node consumes output (rpcmem) directly
        AI tone mapping → ...
```

The single memcpy AHB → rpcmem is unavoidable in 2026 with stock vendor SDKs; see [05](05-npu-backends-zero-copy.md) §4 and open question 7.1 for tracking.

---

## 5. Cited Sources

* PyPI executorch package ─ versions and dates ─ <https://pypi.org/project/executorch/> ─ accessed 2026-05-08.
* ExecuTorch v1.0 release blog ─ <https://pytorch.org/blog/introducing-executorch-1-0/> ─ accessed 2026-05-08.
* ExecuTorch GitHub releases ─ <https://github.com/pytorch/executorch/releases> ─ v1.2.0 tagged 2026-04-01.
* ExecuTorch 1.2 Qualcomm AI Engine Backend docs ─ <https://docs.pytorch.org/executorch/stable/backends-qualcomm.html> ─ accessed 2026-05-08.
* ExecuTorch 1.2 Core ML Backend ─ <https://docs.pytorch.org/executorch/1.0/ios-coreml.html> ─ accessed 2026-05-08.
* ExecuTorch 1.2 Vulkan Backend ─ <https://docs.pytorch.org/executorch/stable/backends-vulkan.html> ─ accessed 2026-05-08.
* ExecuTorch backend partitioning ─ <https://docs.pytorch.org/executorch/stable/compiler-delegate-and-partitioner.html> ─ accessed 2026-05-08.
* ExecuTorch .pte file format ─ <https://docs.pytorch.org/executorch/stable/pte-file-format.html> ─ accessed 2026-05-08.
* ExecuTorch model export ─ <https://docs.pytorch.org/executorch/stable/using-executorch-export.html> ─ accessed 2026-05-08.
* ExecuTorch QNN HTP issue 16465 ─ <https://github.com/pytorch/executorch/issues/16465> ─ filed 2026-01.
* ExecuTorch Vulkan PowerVR issue 17299 ─ <https://github.com/pytorch/executorch/issues/17299> ─ filed 2026-Q1.
* ExecuTorch MediaTek Backend ─ <https://docs.pytorch.org/executorch/stable/backends-mediatek.html> ─ accessed 2026-05-08.
* ONNX Runtime release v1.25.0 GitHub ─ <https://github.com/microsoft/onnxruntime/releases/tag/v1.25.0> ─ accessed 2026-05-08.
* ONNX Runtime QNN EP docs ─ <https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html> ─ accessed 2026-05-08.
* ONNX Runtime CoreML EP docs ─ <https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html> ─ accessed 2026-05-08.
* ONNX Runtime IOBinding ─ <https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html> ─ accessed 2026-05-08.
* ONNX Runtime ORT Mobile format ─ <https://onnxruntime.ai/docs/performance/model-optimizations/ort-format-models.html> ─ accessed 2026-05-08.
* ONNX Runtime QNN HTP shared memory PR #23136 ─ <https://github.com/microsoft/onnxruntime/pull/23136>.
* ONNX Runtime QNN PR #24195 ─ <https://github.com/microsoft/onnxruntime/pull/24195>.
* ONNX Runtime QNN dedicated repo ─ <https://github.com/onnxruntime/onnxruntime-qnn> ─ accessed 2026-05-08.
* Google LiteRT rebrand blog ─ <https://developers.googleblog.com/tensorflow-lite-is-now-litert/> ─ Sept 2024.
* NNAPI deprecation guide ─ <https://developer.android.com/ndk/guides/neuralnetworks/migration-guide> ─ accessed 2026-05-08.
* TFLite Hexagon delegate (deprecated) README ─ <https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/delegates/hexagon/README.md>.
* MNN GitHub releases ─ <https://github.com/alibaba/MNN/releases> ─ 3.5.0 tagged 2026-04.
* NCNN releases ─ <https://github.com/Tencent/ncnn/releases> ─ 2026-01-13.
* NCNN Vulkan notes ─ <https://github.com/Tencent/ncnn/wiki/vulkan-notes> ─ accessed 2026-05-08.
* Apple Core ML overview ─ <https://developer.apple.com/documentation/coreml> ─ accessed 2026-05-08.
* Apple coremltools ML Programs vs Neural Networks ─ <https://apple.github.io/coremltools/docs-guides/source/comparing-ml-programs-and-neural-networks.html>.
* Apple WWDC22 ML digital lounge — IOSurface for ANE ─ <https://yono.ai/articles/wwdc22-machine-learning-digital-lounge/question053a/>.
* Apache TVM Hexagon backend RFC ─ <https://discuss.tvm.apache.org/t/introducing-hexagon-backend/2421>.
* IREE README ─ <https://github.com/iree-org/iree> ─ accessed 2026-05-08.
* OpenXLA blog ─ <https://opensource.googleblog.com/2023/03/openxla-is-ready-to-accelerate-and-simplify-ml-development.html>.
* Qualcomm AIMET repo (BSD-3) ─ <https://github.com/quic/aimet> ─ accessed 2026-05-08.
* Qualcomm AI Hub Workbench release notes ─ <https://workbench.aihub.qualcomm.com/docs/hub/release_notes.html>.
* QAIRT SDK setup ─ <https://docs.qualcomm.com/nav/home/general_setup.html?product=1601111740009302>.
* Snapdragon 8 Elite product brief ─ <https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Elite-Platform-Product-Brief.pdf>.
* DxO DeepPRIME (real-world AI denoise reference) ─ <https://www.dxo.com/technology/deepprime/>.
* Real-World Mobile Image Denoising paper ─ <https://openaccess.thecvf.com/content/CVPR2024/papers/Flepp_Real-World_Mobile_Image_Denoising_Dataset_with_Efficient_Baselines_CVPR_2024_paper.pdf>.
* Deep Joint Demosaicking and Denoising ─ <https://groups.csail.mit.edu/graphics/demosaicnet/data/demosaic.pdf>.
* Restormer paper ─ <https://arxiv.org/abs/2111.09881> ─ CVPR 2022.
* NAFNet repository ─ <https://github.com/megvii-research/NAFNet>.
* Mobile Inference Latency benchmark (USC, 2024) ─ <https://qed.usc.edu/paolieri/papers/2024_edgesys_mobile_inference_benchmark.pdf>.

---

## 6. See also

* [02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md) — the `Buffer` adapter referenced throughout §4.
* [03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md) — owns the precision-conversion insertion and the device-affinity heuristic that picks an engine.
* [05 — NPU Backends & Zero-Copy](05-npu-backends-zero-copy.md) — paired companion; deep-dives on Hexagon, ANE, and the cross-API sync primitives.
* [08 — AI ISP Algorithms](08-ai-isp-algorithms.md) — informs which models we actually need to run, which feeds the latency table here.
* [10 — Plugin Architecture](10-plugin-architecture.md) — owns the C-ABI surface; AI ISP nodes are plugins that consume the `InferenceSession` C++ abstraction described in §4.

---

## 7. Open Questions

1. **AHB → HTP without memcpy.** Is there a vendor extension on Snapdragon 8 Elite that lets the HTP DMA from an `AHardwareBuffer` directly, bypassing rpcmem? Track [PR #24195](https://github.com/microsoft/onnxruntime/pull/24195) and the QAIRT 2.43+ release notes; if available, the §4.5 sequence loses one memcpy (~4 MB at 100 MP, several ms saved).
2. **ANE quantisation accuracy on AI demosaic.** ANE is FP16 only; INT8 weight-quant via Core ML 8 still runs FP16 internally. Some demosaic models (especially those with a final non-linear gain) lose visible quality. Need empirical measurement against in-house DNG corpus before locking the precision policy on Apple.
3. **ExecuTorch Vulkan delegate stability.** Issues #15700 / #17299 indicate divergence on PowerVR (Pixel) and YOLO-style architectures. Plan: prefer XNNPACK on PowerVR, gate Vulkan delegate by GPU vendor in the auto-selection rule.
4. **100 MP single-tile latency.** D2 sets 100 MP single-plane Bayer; AI denoise at full resolution exceeds working memory on every mobile device. v1 will fall back to CPU + tile-stitch on full-res inputs (a temporary D2 violation acknowledged in the open question, not yet locked). Need a tile size that keeps the FP16 working set under 800 MB and quantifies seam artifacts.
5. **HTP context-binary cache invalidation.** Compiled HTP context binaries are SoC-specific. cpipe must key the on-disk cache by `(model_hash, soc_id, qairt_version)`. The QAIRT 2.43 link-job feature lets multiple models share weights — useful for shipping a denoise + demosaic pair, but binds them lifecycle-wise; revisit when locking plugin ABI.
6. **CoreML EP vs ExecuTorch CoreML backend.** Both wrap Core ML; the ExecuTorch path adds an indirection (the runtime calls into the delegate which calls Core ML). On benchmarks the overhead is ~0.5–1 ms per call. For 60 fps preview that is 6 % of frame budget; for batch ISP it is irrelevant. Recommendation: ExecuTorch is fine for v1 batch. Re-evaluate when streaming preview comes back in scope (D5 deferral).
7. **Model-format proliferation in plugins.** D4 says built-in v1, C-ABI v2. The plugin SDK should expose `cpipe::CreateInference(config)` so a plugin can ship `.pte`, `.onnx`, or `.ort` and the host loader chooses the right engine. Decide which formats are mandatory at v2 ABI lock — keep the format list short.
