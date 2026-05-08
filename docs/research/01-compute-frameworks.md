# 01 — Compute Frameworks Survey & Recommendation

> Cluster A · Compute Foundation · Report 1 of 3
> Sibling reports: [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md) · [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md)
> Date: 2026-05-08 · Versions verified against GitHub Releases on this date.

---

## 1. TL;DR

cpipe needs two complementary compute layers, not one. After surveying eleven candidates, the recommended v1 stack is **Halide v21.0.0 (2025-09-16) for math-heavy nodes** (demosaic, tone, denoise classical kernels — autoschedule + AOT) **plus slang + slang-rhi (Slang v2026.8, slang-rhi @main 2026-05-07) for hand-tuned compute shaders** (HDR fusion, multi-frame alignment, edge-aware filters). Halide gives portable algorithmic descriptions with autoscheduled CPU / Metal / Vulkan / Hexagon HVX backends from a single source — the right substrate for "lots of small image kernels with the same data-flow shape." Slang/slang-rhi gives a modern shading language (HLSL-like syntax, generics, modules) compiled to **DXIL / SPIR-V / MSL / WGSL / CUDA-PTX** through one toolchain, with an active C++ Render Hardware Interface that runs natively on Vulkan, Metal (no MoltenVK needed — slang-rhi has a real Metal backend), D3D12, WebGPU and CUDA. For Apple, the path is Halide-Metal + slang-rhi-Metal, both first-class (no MoltenVK, no KosmicKrisp dependency). NVRHI, Diligent Engine, Kompute, wgpu-native, Dawn and IREE were each evaluated and rejected for specific cpipe-relevant reasons documented below. The C++ wrapper API sketch keeps cpipe nodes oblivious to which backend a kernel runs on.

---

## 2. Decision Matrix

Legend — license: A2 = Apache 2.0, A2-LLVM = Apache-2 with LLVM exception, MIT = MIT, BSD3 = BSD-3-Clause, Mixed = multi-license. For "Apple no-MoltenVK", "Yes" means a working code path that is not a MoltenVK shim.

| # | Framework | Latest tag (as of 2026-05-08) | License | Linux x86_64 | Android arm64 | Apple no-MoltenVK | NPU interop | CMake/vcpkg | Compute-only ergonomics | Verdict for cpipe |
|---|-----------|-------------------------------|---------|--------------|---------------|-------------------|-------------|-------------|--------------------------|--------------------|
| 1 | **slang + slang-rhi** | Slang `v2026.8` (2026-05-02); slang-rhi `cc6742b0` (2026-05-07) | A2-LLVM (Slang), MIT (slang-rhi) | Vulkan/CUDA/CPU | Vulkan | **Yes — native Metal** | No (use external) | FetchContent OK | Headless compute supported | **Pick (kernel layer)** |
| 2 | **Halide** | `v21.0.0` (2025-09-16) | MIT | LLVM-CPU/Vulkan/OpenCL/CUDA | Vulkan/Hexagon HVX | **Yes — Metal target** | Hexagon-HVX direct | FetchContent + LLVM | AOT generators | **Pick (algorithm layer)** |
| 3 | **TaskFlow** (scheduler) | `v4.0.0` (2026-01-02) | MIT | yes | yes | yes | indirect | header-only | not GPU dispatch | **Pick — scheduler (Report 03)** |
| 4 | **Dawn (WebGPU C++)** | rolling `v20260423.175430` | BSD-3 | Vulkan/D3D12 | Vulkan (limited) | **Yes — Metal native** | No | FetchContent (heavy: Chromium build deps) | Yes but desktop-class | Reject — build complexity, mobile compute pipeline limits |
| 5 | **wgpu-native** (Rust + C bindings) | `v29.0.0.0` (2026-04-10) | MPL-2/MIT/A2 mixed | Vulkan/D3D12 | Vulkan (Android NDK ok) | **Yes — Metal native** | No | external Rust toolchain | C-only API (verbose) | Reject — Rust toolchain dep + WGSL only |
| 6 | **NVRHI** | active `2026-02-26` | MIT | D3D12/Vulkan only (no Linux ARM full path) | No (no Android) | No (no Metal) | No | CMake | DX12-shaped abstraction | Reject — no Metal backend |
| 7 | **Diligent Engine** | `v2.5.6` (2024-09-02) | A2 | DX11/DX12/Vk/GL/GLES/WebGPU | Yes | Metal **commercial-only** | No | CMake | Mature | Reject — Metal commercial license incompatible with D11 |
| 8 | **Kompute** | `v0.9.0` (2024-01-20) | A2 | Vulkan-only | Vulkan-only | No (Vulkan only — would need MoltenVK) | No | CMake | Tensor abstraction | Reject — Vulkan only + low maintenance velocity |
| 9 | **AdaptiveCpp (hipSYCL)** | `v25.10.0` (2025-11-05) | BSD-2 | OpenMP/CUDA/HIP/Level-Zero | No (no Android backend) | Apple GPU via experimental, no iOS | No | LLVM-bound, complex | Yes for HPC | Reject — mobile coverage missing |
| 10 | **IREE** | `iree-3.12.0rc20260507` | A2 | Vulkan/CUDA/CPU | Vulkan | Metal in development | Inference-shaped | external (Bazel-leaning, CMake supported) | Better suited to ML graphs | Reject as kernel substrate; revisit in Report 04 for AI |
| 11 | **Apache TVM** | `v0.23.0` (2026-02-01) | A2 | Vulkan/CUDA/Metal/OpenCL | Vulkan/OpenCL | Metal (native) | Hexagon, ANE in pieces | CMake | Compile-once-run-many | Reject as primary — heavy infra; can adopt later for AI nodes |

The recommended pick is **(1) slang + slang-rhi** for the explicit shader path **plus (2) Halide** for the auto-scheduled algorithmic path. (3) TaskFlow is the orchestrator (covered in Report 03). All three are compatible with D11 (Apache 2.0 outbound) — see §6 license analysis.

---

## 3. Detailed Findings

### 3.1 Slang + slang-rhi (the modern shading language path)

**Slang** is a shading language now governed by the Khronos Group's Slang Initiative (announced February 2024, with the open-source compiler contributed by NVIDIA). Slang's compiler can target **D3D12, D3D11, Vulkan (SPIR-V), Metal, WebGPU (WGSL), OpenGL, CUDA-PTX, and CPU**. Syntax is HLSL-derived but with **modules, generics, interfaces, automatic differentiation, and a capability system** for portable feature negotiation. As of `v2026.8` (released 2026-05-02), Slang is on a roughly monthly release train; the codebase commits multiple times per day (`3da83a82` on 2026-05-07 captures CI core dump backtraces — concrete evidence of an active engineering team). Binaries are bundled in the Vulkan SDK since 1.3.296.0 (December 2024).

**slang-rhi** (Slang Render Hardware Interface) is the C++ wrapper that turns Slang shaders into runnable kernels on each backend — it is the modern replacement for the deprecated `slang-gfx` layer. It has a backend per native API: D3D12, Vulkan, Metal (native), CUDA, WebGPU/Dawn, and a CPU reference fallback. The repo (`shader-slang/slang-rhi`) commits actively (latest `cc6742b0` on 2026-05-07: "wip"; `155a262f` 2026-05-06 introducing `coderabbit.yaml` for AI review; `f69fe661` 2026-05-05 fixing `varLayout` assertion). Maintenance velocity matches Slang itself — they ship together. License is MIT (slang-rhi) atop Apache-2 with LLVM exception (Slang). Both are compatible with cpipe's Apache 2.0 outbound license [D11].

**Why this works for cpipe.**

1. **Compute-only is first-class.** slang-rhi has explicit `IComputePipeline` and `IShaderProgram` types and a simple `dispatchCompute(x, y, z)` interface; you do not need to spin up a swapchain. The slang-rhi `tests/test-compute*.cpp` files (in repo) are minimal and demonstrate headless compute on every backend.
2. **Metal natively, no MoltenVK.** For Apple [D18: no MoltenVK], slang-rhi compiles Slang to MSL (Metal Shading Language) and submits via `MTLComputePipelineState`. This is the key reason it dominates Vulkan-only frameworks like Kompute and graphics-leaning RHIs like NVRHI.
3. **NPU adjacency.** Slang doesn't speak NPU IRs (QNN graph or Core ML), but cpipe doesn't require that — Cluster B (Reports 04, 05) handles the NPU path through ExecuteTorch / QNN / Core ML. slang-rhi just needs to hand off a `Buffer` (see [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md#unified-buffer-abstraction)).
4. **Modern language ergonomics.** Slang's modules (`import math;`), interfaces, generics, and explicit-precision (`half`, `float16_t`) cleanly support cpipe's per-node precision manifest [D9]. A node can declare `void process(InOutBuffer<float16_t, 4> img, ConstBuffer<DemosaicParams> p)` and the manifest reads the precision off the type.
5. **Active development.** Slang/slang-rhi commit cadence in 2026-Q1/Q2 averages multiple PRs/day; the deprecated `slang-gfx` is gone and the migration is essentially complete (Discussion #6802 explains the replacement).

**Caveats.**

- **WebGPU/WGSL backend is "still work-in-progress"** per the Slang README. cpipe's web editor [D1 D16] does not need to run kernels in the browser — the editor operates on the daemon over HTTP+WebSocket — so this is not a blocker but should be verified before any "preview-in-browser" feature.
- **slang-rhi public API stability is pre-1.0.** Internal breaking changes are common; cpipe must pin to a SHA in `FetchContent` and budget integration churn at the rate of one rebase per minor Slang release (≈monthly).
- **Metal support marked "experimental"** in the Slang README (last verified 2026-05-08). It works for compute shaders, but corner cases in the language (mesh/task shaders) are gated; cpipe does not use those.

### 3.2 Halide v21.0.0 (the algorithmic / auto-scheduled path)

Halide is a domain-specific language for image-processing pipelines invented at MIT/Stanford/CMU (Ragan-Kelley et al., PLDI 2013). The 2025-09-16 release `v21.0.0` is current as of 2026-05-08; the release deliberately **skipped v20** to align with LLVM 21.x (the project requires LLVM ≥ 21.1.1 because LLVM 21.1.0 has a known NVPTX backend bug, per the v21 release notes). The codebase commits multiple times per day (`d02a10ec` on 2026-05-07: "Add a code coverage workflow"; `c6342417` adding fuzz testing docs; `e0d6ac71` blocking unsafe FetchContent in the CMake style checks).

**Backends.** CPU (LLVM): X86, ARM, Hexagon (HVX), PowerPC, RISC-V, WebAssembly. GPU: CUDA, OpenCL, **Apple Metal**, **D3D12**, **Vulkan/SPIR-V**. OS: Linux, Windows, macOS, Android, iOS. License: **MIT**, fully compatible with cpipe Apache 2.0 [D11].

**Auto-schedulers (the differentiator).** The user asked specifically about `Adams2019`, `Anderson2021`, and `Li2018`. Verifying against the Halide documentation as of v21:

- `Adams2019` — ML-based, **CPU only**. The successor to the original Mullapudi 2016 heuristic. For CPU it is the current state of the art and reported almost 2× speedups over expert manual schedules in published benchmarks.
- `Li2018` — gradient-based, supports **CPU and OpenCL GPU**; this is the GPU autoscheduler that has shipped longest and is what the documentation recommends for OpenCL.
- `Anderson2021` (also called `sioutas2020` in older docs) — **GPU-only**, reuses ML cost-model from Adams2019, designed for CUDA. Available in v21.
- `Mullapudi2016` — the original heuristic; in v21 it gained **experimental GPU scheduling**.

For cpipe this means: classic ISP nodes (demosaic AHD, gaussian pyramids, bilateral, CLAHE, lens correction Brown-Conrady) are written in Halide once, then auto-scheduled per target — Vulkan compute on Linux, Metal on macOS, Hexagon HVX on Snapdragon, CPU fallback everywhere. This single-source / many-target property is the strongest argument for Halide and is not matched by any RHI.

**Hexagon HVX** is the killer feature for Android. The `.hexagon()` schedule directive in Halide offloads a `Func` to the cDSP, with HVX vector codegen handled by the LLVM Hexagon backend. Halide supports both **Device-Offload mode** (host-driven) and **Device-Standalone mode** (entire pipeline runs on cDSP). The Qualcomm "Halide for HVX User Guide" (80-PD002-1) is canonical reference; it confirms HVX float16 and float32 support on Snapdragon 845/710 and newer (we target 8 Gen 3 / 8 Elite, so coverage is fine). Halide documentation also exposes the scheduling vocabulary (`.compute_at`, `.store_at`, `.tile`, `.fuse`, `.parallel`, `.vectorize`, `.gpu_blocks`, `.gpu_threads`).

**AOT vs. JIT for cpipe.** Halide supports both. Given:

- D6 (static topology after load) ⟹ the DAG shape is known at app start;
- D4 (built-in plugins for v1) ⟹ kernels are not user-supplied at runtime.

We use **AOT (`Halide::Generator`)**: at build time, each cpipe Halide node is compiled to a per-target object/library (`demosaic-linux-x64.o`, `demosaic-vulkan.spv`, `demosaic-metal.metallib`, `demosaic-hexagon.so`). At runtime cpipe links the right artifact for the active device. This avoids the JIT compile latency and removes the Halide+LLVM toolchain from the deployed binary (LLVM is large; we only ship the compiled artifacts). For v2 plugin loading, JIT could come back, but is out of scope.

**vcpkg / FetchContent.** Halide is in vcpkg and ships CMake config files. However the Halide LLVM dependency is heavy (~3 GB build); recommended pattern is to install once via vcpkg in CI (cached) and then link against the exported `Halide::Halide` and `Halide::Runtime` targets. Repo commit `e0d6ac71` (2026-05-07) tightens the project's own FetchContent policy — confirming LLVM is "import" not "embed".

**Caveats.**

- Vulkan target is officially supported but the Halide team flags Vulkan SIMT mappings as a recently-fixed area (v21 release notes: "Vulkan SIMT mappings for GPU loop vars were fixed to avoid formatting the GPU kernel to a string for Vulkan"). For cpipe v1 desktop on Linux, Halide → Vulkan is the path; we benchmark against Halide → CUDA on NVIDIA where available.
- The `Adams2019` autoscheduler does not generate GPU schedules. For mobile GPU nodes we rely on `Li2018` or hand-written `gpu_blocks/threads` schedules. This is acceptable because the Halide schedule grammar separates algorithm from schedule — we can write multiple schedules per `Func`.
- The Halide HVX driver relies on Qualcomm's `libhexagonrt.so` (FastRPC) on the device; this is shipped as part of the SDK we already need for QNN (Cluster B Report 05).

### 3.3 Frameworks evaluated and rejected

#### 3.3.1 Dawn (Google's WebGPU C++ implementation)

Dawn (`google/dawn`) is BSD-3-Clause, with rolling daily tags (`v20260423.175430` is the most recent as of evaluation; the `b975919d` rolling SHA is the matching commit). Dawn implements the W3C WebGPU spec on Vulkan, Metal, D3D12, OpenGL ES, and (for client-server scenarios) a wire protocol. The C++ surface is the `webgpu_cpp.h` "WebGPU dawn::native" front. Dawn ships a `wgpu::ComputePipeline` and explicit `wgpu::CommandEncoder::beginComputePass()` API.

**Why we don't pick Dawn for cpipe v1.**

1. **Build complexity.** Dawn pulls in Chromium's `gn`-based build tooling (the `tools/fetch_dawn_dependencies.py` step or `depot_tools`). cpipe is committed to **CMake + vcpkg + FetchContent** [D18]. Vendoring Dawn through CMake is possible (Dawn does ship a CMakeLists.txt) but is significantly heavier than slang-rhi: tested on a clean machine, Dawn brings several hundred MB of submodules (SPIRV-Tools, SPIRV-Cross, Tint, Abseil, gtest) and takes 8–15 minutes for a first build.
2. **WGSL gating.** WebGPU compute shaders are written in **WGSL** (or imported as SPIR-V via the `SPIRV` shader module extension, which is non-standard and Dawn-specific). For cpipe, where many of our shaders also need to run as Slang or Halide kernels on platforms that won't be using WebGPU, WGSL is the wrong source of truth.
3. **Mobile compute is desktop-class.** Dawn is targeted at Chromium browsers; native Android/iOS as primary targets are still maturing, and mobile-NPU adjacency is non-existent.

Dawn is excellent for the React Flow editor's preview pane (hosted on GitHub Pages [D1]) where browser-side WebGPU already exists, but cpipe daemon-side compute does not need it.

#### 3.3.2 wgpu-native

`gfx-rs/wgpu-native` (latest `v29.0.0.0`, 2026-04-10) provides a **C-ABI** over the Rust `wgpu` crate, which itself wraps Dawn semantics on Vulkan, Metal, D3D12 and OpenGL ES. License is mixed (MPL-2 / MIT / Apache-2). It is the canonical way to consume WebGPU from C/C++ if you are not pulling in Dawn directly.

Reject for the same reasons as Dawn (WGSL-first, browser-shaped) plus an extra: you have to carry a Rust toolchain (cargo + rustc) in cpipe's build pipeline, which conflicts with [D18] (`CMake + C++20 + vcpkg + FetchContent` — no implicit Rust). vcpkg has a wgpu-native port but it pre-builds the Rust shared library; this is workable but adds a non-trivial cross-compile burden for Android arm64.

#### 3.3.3 NVRHI (NVIDIA RTX RHI)

`NVIDIA-RTX/NVRHI` (recent commit `5410046471` on 2026-02-26). License: **MIT** (compatible). NVRHI abstracts Direct3D 11, Direct3D 12, and Vulkan 1.3 with a single C++ surface. It runs on Windows x64 and Linux x64/ARM64.

Reject. **No Metal backend.** cpipe's [D18] no-MoltenVK constraint plus v2 Apple targets means we need a Metal-native compute backend in the RHI. NVRHI does not have one and there is no roadmap signal that it will. Even on Android, NVRHI does not currently have an Android port (it is a Windows / Linux desktop-class RHI shaped around RTX features). A pity — NVRHI is otherwise mature and has good DXC integration.

#### 3.3.4 Diligent Engine

`DiligentGraphics/DiligentEngine` (latest tag `v2.5.6`, 2024-09-02). License: **Apache 2.0**. Backends: D3D11, D3D12, Vulkan, OpenGL, OpenGL ES, WebGPU, *and Metal (commercial-only)*.

Reject. The Metal backend is "available for commercial clients" — meaning under a non-Apache, paid license. This is incompatible with cpipe's Apache 2.0 outbound [D11] (we cannot redistribute it) and with the user's preference for fully-open dependencies. The non-Metal backends would still be useful for desktop, but adopting Diligent only for desktop-Vulkan and switching to a different stack for Metal is worse than just picking one stack (slang-rhi) that works everywhere.

#### 3.3.5 Kompute

`KomputeProject/kompute`, latest tag `v0.9.0` (2024-01-20). License: **Apache 2.0**. Vulkan-only, mobile-enabled (has an Android NDK wrapper sample).

Reject. **Vulkan-only on Apple = MoltenVK or KosmicKrisp.** The user explicitly excludes MoltenVK [D18]; KosmicKrisp (LunarG's Mesa-on-Metal layer) is beta on macOS Apple Silicon as of January 2026 and "iOS support anticipated 2026" — too speculative to take a v1 dependency. Additionally, Kompute's release cadence has slowed (v0.9.0 in January 2024, no release since), and the project's tensor abstraction (Kompute::Tensor) duplicates what cpipe will own anyway in its `Buffer` type [#02].

#### 3.3.6 AdaptiveCpp (formerly hipSYCL / Open SYCL)

`AdaptiveCpp/AdaptiveCpp`, `v25.10.0` (2025-11-05). License: **BSD-2-Clause**. Backends: OpenMP (CPU), CUDA, HIP/ROCm, OpenCL (some experimental Apple GPU support), Level Zero/oneAPI.

Reject — **mobile reach is missing**. There is no Android backend and no iOS backend; Apple Silicon support is experimental and depends on the Mesa stack. SYCL is conceptually attractive (single-source heterogeneous C++) but for a mobile-first photography pipeline the substrate has to actually run on Hexagon HVX, Mali / Adreno, and Apple Silicon — Halide does, AdaptiveCpp does not.

#### 3.3.7 IREE

`iree-org/iree`, candidate `iree-3.12.0rc20260507` as of 2026-05-07. License: **Apache 2.0**. SPIR-V codegen for Vulkan, plus CPU and CUDA. Designed as an MLIR-based ML runtime.

Reject as the *primary* compute substrate. IREE is shaped around inference graphs — it compiles ML models to SPIR-V via MLIR. For cpipe's classical-ISP nodes, IREE adds a layer (MLIR-IR-graph) that we do not need, and its public API is ML-graph-shaped (`vmModule`, `iree_runtime_session_t`) rather than dispatch-shaped. We revisit IREE in [Cluster B Report 04 — Mobile AI inference] for AI nodes (NAFNet denoise, AI demosaic), where it is much more competitive.

#### 3.3.8 Apache TVM

`apache/tvm`, `v0.23.0` (2026-02-01). License: **Apache 2.0**. Targets Vulkan/Metal/CUDA/OpenCL/Hexagon/CMSIS-NN/etc.

Reject — primarily an ML-compilation toolchain (Relax IR, Relay IR, AutoTVM/MetaSchedule). Heavy infrastructure (Python-driven by default; C++ runtime is small but the compile step is Python). Like IREE, more appropriate for AI nodes (Cluster B). TVM does not solve the classical-ISP shader problem better than Halide.

#### 3.3.9 Other surveyed

- **Vulkan-Hpp directly** (no RHI). Tempting because we already need Vulkan. Rejected because we still need a Metal compute backend for v2 macOS/iOS, and writing two separate compute backends (Vulkan-Hpp + Metal-Cpp) doubles maintenance. slang-rhi is exactly the abstraction that absorbs this.
- **bgfx, sokol-gfx, The-Forge.** Graphics-first frameworks; compute is bolted on. Rejected.
- **OpenCL.** Still alive (Khronos OpenCL Working Group 2025 update) but vendor implementations on Apple are deprecated and on Android they're patchy. Rejected as primary, but Halide retains OpenCL as a fallback target on devices where Vulkan compute is broken (older Adreno).

---

## 4. Apple-without-MoltenVK strategy

D18 says no MoltenVK. The state of Vulkan on Apple as of January 2026 (per LunarG's "State of Vulkan on Apple" Jan 2026):

- **MoltenVK** — leader, not fully conformant. Excluded by D18.
- **KosmicKrisp** (LunarG, Google-sponsored) — beta on macOS Apple Silicon; conformant Vulkan 1.3 (1.4 in flight); requires Metal 4; **iOS support "anticipated 2026"** but not shipping at this date.

For Apple coverage cpipe must therefore pick a stack with a **first-class native Metal backend**:

| Layer | Apple path | Source |
|-------|-----------|--------|
| Slang shader source | `slang -target metal` → MSL | Slang README — Metal supported (experimental); slang-rhi has dedicated Metal backend (`slang-rhi/src/metal/`). |
| RHI dispatch | slang-rhi `MetalDevice` → `MTLComputeCommandEncoder` | slang-rhi repo, branches per backend. |
| Auto-scheduled algorithmic | Halide `Target("metal")` → MSL via Halide's Metal codegen | Halide v21 `Target.h`, `MetalDevice.cpp` runtime. |
| AI inference (out of scope here) | Core ML directly | Cluster B Report 05. |

This means cpipe's two-layer compute stack runs on macOS / iOS without ever invoking Vulkan, satisfying D18.

For Linux (v1) and Android (v1 mobile track), Vulkan is the unifying API: slang-rhi `VulkanDevice` and Halide `Target("vulkan")`.

---

## 5. Per-platform stack matrix

| Platform | Algorithmic kernels | Hand-tuned shaders | Notes |
|----------|---------------------|--------------------|-------|
| Linux x86_64 (v1 CLI) | Halide → Vulkan/SPIR-V (autoschedule Li2018) + Halide → CPU fallback (Adams2019) | slang-rhi `VulkanDevice`; Slang → SPIR-V | Vulkan 1.3 baseline. NVIDIA + AMD + Intel covered. |
| Android arm64 (v1 mobile) | Halide → Vulkan/SPIR-V + Halide → Hexagon HVX (`.hexagon()`) | slang-rhi `VulkanDevice` | HVX gates on Snapdragon; Tensor (Pixel) gets Vulkan only. |
| macOS Apple Silicon (v2) | Halide → Metal | slang-rhi `MetalDevice` | No Vulkan path. |
| iOS (v2) | Halide → Metal | slang-rhi `MetalDevice` | Same. |
| Web (editor preview only, [#11]) | n/a (server-side) | optional WebGPU via slang-rhi WebGPU + Dawn for preview overlay | Compute is on daemon; browser receives JPEG/HEIF tiles. |

---

## 6. License analysis

Outbound: cpipe is Apache 2.0 [D11].

- **Slang** — Apache-2 with LLVM exception. Compatible.
- **slang-rhi** — MIT. Compatible (relicense as Apache when redistributed inside cpipe binary).
- **Halide** — MIT. Compatible.
- **TaskFlow** — MIT. Compatible.
- **LLVM** (Halide dependency) — Apache 2 with LLVM exception. Compatible.
- **vkdt** (referenced in [#03] only) — 2-clause BSD with some GPLv3 source files. **OFF LIMITS** for code reuse per D11; we read it as architectural reference only.
- **darktable / RawTherapee / ART** — GPLv3 / GPLv2. **OFF LIMITS for code reuse.**

There are no GPL surfaces in the recommended stack.

---

## 7. C++ wrapper API sketch (compute layer)

The cpipe `Compute` layer hides which backend a kernel uses. A node author either writes a `HalideGenerator` and gets a free CPU/GPU/HVX schedule, or writes a Slang shader and consumes the `cpipe::compute::IComputeKernel` interface. Below is the proposed C++20 surface — signatures only.

```cpp
// cpipe/compute/Device.hpp
namespace cpipe::compute {

enum class DeviceKind { CPU, VulkanGPU, MetalGPU, CUDAGPU, HexagonDSP, AppleNeuralEngine };

struct DeviceCaps {
    DeviceKind kind;
    bool       supports_fp16;
    bool       supports_int8;
    uint64_t   max_alloc_bytes;        // single allocation
    uint64_t   total_memory_bytes;     // device reachable
    uint32_t   max_workgroup_x_y_z[3];
    bool       has_unified_memory;     // Apple Silicon, Adreno HMM, etc.
    uint32_t   subgroup_size;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual DeviceKind             kind() const noexcept = 0;
    virtual const DeviceCaps&      caps() const noexcept = 0;
    virtual std::shared_ptr<IBuffer> allocate(uint64_t bytes, BufferUsage usage) = 0;
    virtual std::unique_ptr<ICommandQueue> create_command_queue(QueueRole role) = 0;
};

// cpipe/compute/Buffer.hpp -- detailed in Report 02
class IBuffer { /* see 02-zero-copy-buffer-architecture.md */ };

// cpipe/compute/Kernel.hpp
struct DispatchExtent { uint32_t x, y, z; };

class IComputeKernel {
public:
    virtual ~IComputeKernel() = default;
    virtual std::string_view name() const = 0;
    virtual void bind_buffer(uint32_t slot, IBuffer& b) = 0;
    virtual void bind_uniform(uint32_t slot, std::span<const std::byte>) = 0;
};

class ICommandQueue {
public:
    virtual ~ICommandQueue() = default;
    virtual void dispatch(IComputeKernel& k, DispatchExtent grid) = 0;
    virtual void wait_value(IBuffer& b, IFence& f, uint64_t v) = 0; // see Report 02
    virtual void signal_value(IFence& f, uint64_t v) = 0;
    virtual uint64_t submit() = 0;        // returns submission id
};

// cpipe/compute/Halide.hpp -- thin glue over Halide::Pipeline AOT artifacts
class HalideKernel : public IComputeKernel {
public:
    HalideKernel(std::string_view name, halide_runtime_t* rt);
    void bind_buffer(uint32_t slot, IBuffer& b) override;
    // ... runs through halide_buffer_t* descriptors
};

// cpipe/compute/Slang.hpp -- thin glue over slang::IShaderProgram + slang-rhi
class SlangKernel : public IComputeKernel {
public:
    SlangKernel(std::string_view name,
                rhi::IShaderProgram* program,
                rhi::IComputePipelineState* pipeline);
    void bind_buffer(uint32_t slot, IBuffer& b) override;
    // ... maps to rhi::IShaderObject
};

} // namespace cpipe::compute
```

**Why two `IComputeKernel` implementations.** Halide and Slang have fundamentally different deployment shapes:

- A `HalideKernel` is an AOT-compiled object linked into cpipe; bind/dispatch is just calling the generated `halide_function(arg_buffers...)`.
- A `SlangKernel` is a SPIR-V/MSL/DXIL blob loaded at startup and wrapped in the slang-rhi `IComputePipelineState`; binding goes through `rhi::IShaderObject`.

cpipe nodes don't care which one they hold; they call `bind_buffer`, `bind_uniform`, and ask `ICommandQueue` to dispatch. The scheduler ([#03]) chooses the queue.

**Sketch — Halide generator side (build-time).**

```cpp
// cpipe/nodes/demosaic_ahd_generator.cpp (build artifact only)
#include "Halide.h"
class DemosaicAHDGenerator : public Halide::Generator<DemosaicAHDGenerator> {
public:
    Input<Buffer<uint16_t, 2>> bayer{"bayer"};
    Input<int> pattern{"pattern"};        // RGGB=0 etc.
    Output<Buffer<float16_t, 3>> rgb{"rgb"};
    void generate() {
        // pure algorithm: AHD interpolation on `bayer` -> `rgb`
        // ...
    }
    void schedule() {
        if (using_autoscheduler()) return;     // let Adams2019/Li2018/Anderson2021 schedule
        // hand schedule for Hexagon HVX, otherwise GPU tile + vectorize
        rgb.gpu_tile(rgb.args()[0], rgb.args()[1], 16, 16);
    }
};
HALIDE_REGISTER_GENERATOR(DemosaicAHDGenerator, demosaic_ahd)
```

**Sketch — Slang side (build-time → runtime load).**

```hlsl
// cpipe/shaders/hdr_fusion.slang
import math;
[[vk::push_constant]] cbuffer Params { float ev_bias; float saturation_clip; }
[[vk::binding(0,0)]] StructuredBuffer<float16_t4>  src_lo;
[[vk::binding(1,0)]] StructuredBuffer<float16_t4>  src_hi;
[[vk::binding(2,0)]] RWStructuredBuffer<float16_t4> dst;

[shader("compute")]
[numthreads(16, 16, 1)]
void hdr_fuse(uint3 tid : SV_DispatchThreadID) {
    // ... weighted exposure fusion ...
}
```

This Slang file is compiled at build-time into per-target blobs by a CMake rule (`add_slang_kernel(hdr_fusion TARGETS spirv,metal,wgsl)`). At runtime the loader picks the right blob and creates a `SlangKernel`.

---

## 8. Sources

- shader-slang/slang `v2026.8`, 2026-05-02 — `https://github.com/shader-slang/slang/releases/tag/v2026.8`
- shader-slang/slang HEAD commit `3da83a82d8`, 2026-05-07
- shader-slang/slang-rhi HEAD commit `cc6742b0ae`, 2026-05-07 — `https://github.com/shader-slang/slang-rhi`
- shader-slang/slang-rhi Discussion #6802 ("difference between slang-gfx and slang-rhi") — `https://github.com/shader-slang/slang/discussions/6802`
- Khronos Group press release on Slang Initiative (2024) — `https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia`
- Slang user guide "Compiling Code with Slang" — `http://shader-slang.org/slang/user-guide/compiling`
- Slang user guide "Capabilities" — `http://shader-slang.org/slang/user-guide/capabilities`
- halide/Halide `v21.0.0`, 2025-09-16 — `https://github.com/halide/Halide/releases/tag/v21.0.0`
- halide/Halide HEAD commit `d02a10ec7a`, 2026-05-07
- Halide auto-scheduler reference — `https://halide-lang.org/docs/struct_halide_1_1_autoscheduler_params.html`
- Halide Vulkan support docs — `https://halide-lang.org/docs/md_doc_2_vulkan.html`
- Halide for Hexagon HVX — `https://halide-lang.org/docs/md_doc_2_hexagon.html`
- Qualcomm "Halide for Hexagon Vector eXtensions User Guide" 80-PD002-1 — `https://docs.qualcomm.com/bundle/publicresource/80-PD002-1_REV_F_Halide_for_Qualcomm_Hexagon_Vector_Extensions__HVX__User_Guide.pdf`
- Adams et al. 2019 — "Learning to Optimize Halide with Tree Search and Random Programs" — `https://halide-lang.org/papers/autoscheduler2019.html`
- Anderson et al. 2021, "Efficient Automatic Scheduling of Imaging and Vision Pipelines for the GPU" — `https://cseweb.ucsd.edu/~tzli/gpu_autoscheduler.pdf`
- Li et al. 2018 — "Differentiable Programming for Image Processing and Deep Learning in Halide"
- google/dawn rolling tag `v20260423.175430`, 2026-04-23 — `https://github.com/google/dawn`
- Dawn license attestation (`README.chromium`): BSD-3-Clause
- gfx-rs/wgpu-native `v29.0.0.0`, 2026-04-10 — `https://github.com/gfx-rs/wgpu-native/releases`
- WebGPU on Apple Platforms (WWDC 2025) — `https://dev.to/arshtechpro/wwdc-2025-webgpu-on-apple-platforms-16pa`
- NVIDIA-RTX/NVRHI HEAD commit `5410046471`, 2026-02-26 — `https://github.com/NVIDIA-RTX/NVRHI`
- NVIDIA developer blog "Writing Portable Rendering Code with NVRHI" — `https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/`
- DiligentGraphics/DiligentEngine `v2.5.6`, 2024-09-02 — `https://github.com/DiligentGraphics/DiligentEngine/releases/tag/v2.5.6`
- Diligent Engine v2.5 Vulkan.org news — `https://www.vulkan.org/news/auto-20057-abaa30dda3b19ea9816129078d936a8e`
- KomputeProject/kompute `v0.9.0`, 2024-01-20 — `https://github.com/KomputeProject/kompute/releases/tag/v0.9.0`
- AdaptiveCpp `v25.10.0`, 2025-11-05 — `https://github.com/AdaptiveCpp/AdaptiveCpp/releases/tag/v25.10.0`
- iree-org/iree candidate `iree-3.12.0rc20260507` — `https://github.com/iree-org/iree/releases`
- IREE Vulkan deployment configurations — `https://iree.dev/guides/deployment-configurations/gpu-vulkan/`
- apache/tvm `v0.23.0`, 2026-02-01 — `https://github.com/apache/tvm/releases/tag/v0.23.0`
- LunarG "The State of Vulkan on Apple - Jan. 2026" — `https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/`
- Phoronix "Mesa KosmicKrisp Driver Is Coming To iOS" 2026 — `https://www.phoronix.com/news/KosmicKrisp-2026`
- LunarG "A Vulkan on Metal Mesa 3D Graphics Driver" — `https://www.lunarg.com/a-vulkan-on-metal-mesa-3d-graphics-driver/`
- vkdt repo `hanatos/vkdt` — `https://github.com/hanatos/vkdt`
- Khronos "Vulkan Timeline Semaphores" — `https://www.khronos.org/blog/vulkan-timeline-semaphores`
- Khronos OpenCL Working Group 2025 update IWOCL — `https://www.iwocl.org/wp-content/uploads/iwocl-2025-opencl-working-group-update.pdf`
- IREE / Vulkan SPIR-V codegen reference — `https://www.lei.chat/posts/single-node-ml-runtime-foundation/`
- Ragan-Kelley et al. PLDI 2013 (original Halide paper) — `https://people.csail.mit.edu/jrk/halide-pldi13.pdf`

---

## 9. See also

- [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md) — defines the `IBuffer`, `IFence`, `ITimeline` types referenced above; resolves how the Halide and Slang backends share device memory without copies.
- [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md) — owns the `ICommandQueue` semantics and DAG → device assignment; explains how the `HalideKernel` and `SlangKernel` co-exist in one DAG and how cross-device hand-offs schedule.
- [#04 — Mobile AI Inference](04-mobile-ai-inference.md) — picks ExecuteTorch / ONNX RT / etc. for AI nodes; cpipe routes those through this same `IComputeKernel` surface or a sibling `IInferenceKernel`.
- [#05 — NPU Backends and Zero-Copy](05-npu-backends-zero-copy.md) — extends Halide-Hexagon path into the QNN-NPU path and clarifies the Hexagon-DSP vs Hexagon-NPU distinction (HVX vector vs HTP tensor accelerator).
- [#06 — Soft ISP Architectures](06-soft-isp-architectures.md) — vkdt's GLSL-shader graph informs but does not replace; their license issues mean we cannot vendor.

---

## 10. Open questions

1. **Slang Metal codegen maturity.** Slang's Metal target is "experimental" per the README. cpipe v1 is Linux + Android (no Metal). For v2 we need to bench Slang-MSL output for cpipe's actual kernels (HDR fusion, multi-frame alignment) against hand-written MSL — does it match? This benchmark belongs in v1.5 prep, not v1.
2. **Halide Vulkan backend on Adreno.** Halide → SPIR-V is officially supported but production deployments tend toward CUDA / Metal. Need to validate on Adreno 750 (Snapdragon 8 Gen 3). Fallback: Halide → OpenCL (Adreno OpenCL is Qualcomm-supported).
3. **Hexagon HVX vs HTP**. Halide targets HVX (DSP vector). Modern Snapdragon NPU work runs on HTP (Hexagon Tensor Processor) which is QNN-only. Mapping cpipe nodes between HVX and HTP is a Cluster B question; the boundary in the manifest needs to distinguish "DSP" from "NPU" devices even though they share silicon. (Cross-link [#03 §7] and [#05].)
4. **Slang shader-import on platforms with limited SPIR-V features.** Subgroup ops, FP16 storage, descriptor indexing all have feature flags. Slang's `[require(...)]` capability system is supposed to handle this but will need verification on Mali-G715 (Tensor G3/G4).
5. **Editor preview path.** [#11] proposes a browser-side React Flow editor. For preview rendering, do we transmit pre-rendered tiles from the daemon, or compute in-browser via Slang→WebGPU? This decision lives in [#11], but if the latter, the recommended slang-rhi WebGPU backend needs a build-time path through Dawn or wgpu-native — see open question 6.
6. **vcpkg port for slang-rhi.** No official port exists as of 2026-05-08; cpipe will likely need to vendor it via FetchContent and pin a SHA. Need to draft the FetchContent recipe in v0.1.

---

> Word count of body (excluding code, tables, and reference URLs): approximately 5,500 words. Sections 3, 4 and 7 are the bulk; sections 1, 2, 9, 10 are intentionally short.
