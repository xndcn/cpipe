# Technology Selections

## Overview

This document records every major dependency chosen for cpipe, the rationale behind each choice, alternatives considered, and relevant trade-offs. All versions are pinned to the latest stable release as of April 2026. See [architecture.md](architecture.md) for how these components fit together.

## Language and Build

### C++20

- **Rationale**: Concepts for generic plugin interfaces, designated initializers for readable configuration structs, `std::span` for zero-copy buffer views, `std::format` for logging, coroutines for potential async I/O. Mature toolchain support on all target platforms.
- **Compiler targets**:
  - GCC 13+ (Linux)
  - Clang 15+ (macOS, Android NDK r26+, Linux)
  - MSVC 2022 17.4+ (Windows)
- **C++23 policy**: Features like `std::expected` may be used if guarded behind `__has_include` or feature-test macros (`__cpp_lib_expected`). Provide polyfill (e.g., tl::expected) for compilers without support.
- **Alternatives considered**: C++17 (insufficient -- no concepts, no designated initializers), Rust (excellent safety but Halide integration is C++ native, and AI inference SDKs are C++ first-class).

### CMake 3.25+

- **Rationale**: CMake presets for reproducible builds, improved FetchContent, native vcpkg integration via toolchain file, and good Android NDK cross-compilation support.
- **Usage**: CMake presets (`CMakePresets.json`) define build configurations for Linux, macOS, Windows, and Android.
- **Alternatives considered**: Meson (weaker vcpkg integration), Bazel (steep learning curve, overkill for this project size).

## Package Management

### vcpkg (Primary)

- **Rationale**: Largest C++ package registry, CMake-native manifest mode, excellent cross-platform support, Microsoft-maintained.
- **Usage**: `vcpkg.json` manifest in repository root declares all dependencies. Toolchain integrated via `CMAKE_TOOLCHAIN_FILE`.
- **Android**: Set `VCPKG_TARGET_TRIPLET=arm64-android` and `ANDROID_NDK_HOME`. Chain Android NDK toolchain via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`.
- **Managed packages**: spdlog, nlohmann-json, CLI11, GTest, Google Benchmark, libheif, libraw, libwebsockets

### FetchContent (Secondary)

- **Rationale**: For dependencies not in vcpkg, requiring specific versions, or needing source-level integration.
- **Managed packages**: Halide v21.0, Taskflow v4.0.0, ExecuTorch, ONNX Runtime
- **Policy**: Pin to exact git tags. Use `FetchContent_MakeAvailable` with `EXCLUDE_FROM_ALL` to avoid building unnecessary targets.

## GPU Compute

### Halide v21.0 (Primary Compute)

- **Version**: v21.0.0 (released 2025-09-16, aligned with LLVM 21)
- **Rationale**: Schedule/algorithm separation allows a single codebase to target multiple backends. Proven in production image processing (Google, Adobe, Qualcomm). Extensive autoscheduler support.
- **GPU backends**: Vulkan (Android/Linux/Windows), Metal (macOS/iOS), CUDA (optional, Linux/Windows), OpenCL (legacy fallback)
- **Android/iOS**: AOT compilation targeting arm64-v8a via NDK. HelloAndroidCamera2 example ships with Halide. iOS Simulator target added in v21.
- **GPU scheduling**: Experimental GPU autoscheduler (Mullapudi2016) in v21. Manual `gpu_blocks`/`gpu_threads` scheduling for production nodes.
- **Integration**: Via FetchContent or prebuilt release. `find_package(Halide)` for CMake integration.
- **Alternatives considered**: Raw Vulkan compute shaders (too much boilerplate for every node), OpenCL (deprecated on Apple), TVM (ML-focused, not image processing DSL).
- **Trade-off**: Halide scheduling overhead is acceptable for most nodes. Native Vulkan/Metal compute shaders used only when profiling shows >20% improvement over Halide-generated code.

### Native Vulkan / Metal (Optimization Only)

- **Rationale**: For measured performance bottlenecks where Halide-generated code is suboptimal.
- **Decision criteria**: Profile node with Halide first. Switch to native only if >20% latency improvement is measured on target hardware.
- **Expected candidates**: Simple LUT application, buffer format conversion, operations with non-standard memory access patterns.

## AI Inference

### Unified InferenceBackend Abstraction

Both backends are exposed through a common `InferenceBackend` interface. Pipeline JSON specifies preferred backend per node; runtime falls back to alternate backend or CPU if preferred is unavailable.

### ExecuTorch (Meta)

- **Version**: v1.2.0 (released 2026-04-01, aligned with PyTorch 2.11)
- **Rationale**: First-party PyTorch mobile runtime with 50KB base footprint. Native NPU delegate support for Qualcomm QNN, MediaTek Neuron, and Samsung Exynos.
- **Primary use**: Mobile inference path (Android/iOS)
- **Key features**:
  - XNNPACK delegate for high-performance CPU (ARM NEON, x86 AVX)
  - Vulkan delegate for GPU inference (INT8 quantized)
  - QNN delegate with CDSP Direct Mode for Qualcomm NPU
  - 50KB base runtime, selective operator registration
- **C++ integration**: CMake via `add_subdirectory`. Android cross-compile with NDK r28c.
- **Model format**: `.pte` (ExecuTorch Program)

### ONNX Runtime (Microsoft)

- **Version**: v1.24.4 (released 2026-03-17)
- **Rationale**: Cross-framework model support (PyTorch, TensorFlow via ONNX), mature C/C++ API, broad execution provider ecosystem.
- **Primary use**: Desktop inference, models not easily exported to ExecuTorch format
- **Key features**:
  - CUDA Execution Provider (NVIDIA desktop GPU)
  - CoreML Execution Provider (macOS/iOS)
  - QNN Execution Provider (Qualcomm Android)
  - NNAPI Execution Provider (Android fallback)
  - XNNPACK for CPU
- **Model format**: `.onnx`

### Comparison

| Aspect | ExecuTorch | ONNX Runtime |
|--------|-----------|--------------|
| Ecosystem | PyTorch-native | Cross-framework |
| Base footprint | ~50KB | ~1-5MB (mobile) |
| Best for | Mobile, constrained devices | Desktop, multi-framework |
| NPU backends | QNN, MediaTek, Samsung | QNN, NNAPI |
| Maturity | Production at Meta (2024+) | Production since 2019 |

## DAG Scheduling

### Taskflow v4.0.0

- **Version**: v4.0.0 (released 2026-01-02)
- **Rationale**: Mature C++20 DAG execution library with work-stealing scheduler, dynamic task graphs, conditional branching, and composable subflows. GPU tasking via `tf::cudaGraph`.
- **Key features**:
  - Static and dynamic task graphs
  - Conditional tasking (if/else, switch in DAG)
  - Composable modules (sub-graph reuse)
  - `tf::cudaGraph` for NVIDIA GPU tasks
  - Bulk scheduling with reduced atomic overhead
- **Custom device layer**: Taskflow has no native NPU support. We wrap GPU and NPU calls as regular Taskflow tasks. A custom `DeviceAllocator` manages device assignment based on node hints, device availability, and current load.
- **Integration**: Header-mostly library via FetchContent or vcpkg.
- **Alternatives considered**: Intel TBB Flow Graph (heavier dependency, less active), custom scheduler (too much engineering risk), EnTT (ECS-focused, not DAG-native).

## Buffer Management

### BufferPool + BufferDescriptor

- **Strategy**: Pre-allocated ring buffer pool with reference counting. Buffers are typed by `BufferDescriptor` (width, height, pixel format, stride, device affinity).
- **Android**: `AHardwareBuffer` backend enables zero-copy sharing between Camera2, Vulkan GPU, NNAPI/NPU, and hardware encoder. Import into Vulkan via `VK_ANDROID_external_memory_android_hardware_buffer`.
- **Desktop**: Vulkan buffer with host-visible mapping, or plain host-allocated memory with 64-byte alignment for SIMD.
- **Synchronization**: Android sync fences for cross-device access. Vulkan semaphores/fences for GPU-GPU and GPU-CPU synchronization.
- **Note**: Not all NPU backends support AHardwareBuffer zero-copy. Qualcomm QNN and MediaTek Neuron do; Samsung Exynos support varies by SoC.

## Serialization

### nlohmann/json

- **Rationale**: STL-like API (`json["key"]`), header-only option, ubiquitous in C++ projects, excellent documentation.
- **Usage**: Pipeline JSON parsing, config files, JSON-RPC WebSocket messages, plugin parameter schemas.
- **Alternative**: simdjson (read-only, ~10x faster parsing). May use for pipeline loading hot path if profiling shows JSON parsing as bottleneck.

### JSON Schema

- **Usage**: Pipeline format validated against JSON Schema before execution. Each plugin exports a parameter schema (JSON Schema format) that the pipeline editor uses to auto-generate UI controls.

## CLI

### CLI11

- **Rationale**: Header-only, expressive subcommand support, good error messages, lightweight.
- **Subcommands**: `process`, `list-plugins`, `inspect`, `benchmark`, `serve`
- **Alternatives considered**: cxxopts (no subcommand support), Boost.ProgramOptions (heavy dependency), argparse (Python-esque but less C++ idiomatic).

## Logging

### spdlog

- **Rationale**: High-performance logging with format string support, multiple sinks, header-only option.
- **Sinks**: stdout (colored), rotating file, Android logcat (`android_sink`)
- **Pattern**: `[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v`
- **Levels**: trace (Halide scheduling details), debug (node execution), info (pipeline lifecycle), warn/error/critical.

## Output Format

### HEIF (No JPEG)

- **Rationale**: Superior compression ratio (~50% vs JPEG at same quality), 10-bit HDR support, modern standard supported by all major platforms.
- **Desktop**: libheif v1.21.2 with x265 encoder (HEIC) or AOM encoder (AVIF).
  - libheif supports HEIC, AVIF, VVC, JPEG-2000 in a single library.
  - C API with header-only C++ wrapper.
  - HDR metadata via color transformation matrices.
- **Android**: Platform native path -- MediaCodec H.265/HEVC hardware encoder + MediaStore for saving.
- **AVIF**: Supported as alternative format via libheif's AV1 backend. Decision on default (HEIC vs AVIF) deferred to M5 based on encoder maturity.
- **Alternatives rejected**: JPEG (legacy, 8-bit only, inferior compression), PNG (lossless but huge files, no HDR), WebP (limited HDR support).

## DNG Parsing

### libraw (Desktop)

- **Version**: v0.22.1 (released 2026-04-06)
- **Rationale**: De facto standard for RAW/DNG parsing. Supports DNG 1.7, JpegXL DNG, transparency masks.
- **Key features**: DNG metadata extraction (BlackLevel, ColorMatrix, ForwardMatrix, NoiseProfile, OpcodeList), thumbnail extraction, basic processing.

### Platform API (Android)

- **Rationale**: Android's Camera2 API and `DngCreator`/`ImageReader` handle DNG capture natively. Using platform APIs avoids shipping libraw's large native library on mobile.

## WebSocket

### libwebsockets or uWebSockets

- **Final decision**: Deferred to M3 milestone.
- **Requirements**: JSON-RPC protocol, low latency for live parameter tuning, binary frame support for preview images.
- **libwebsockets**: Mature, well-documented, event-loop based. Used by many production systems.
- **uWebSockets**: Higher throughput, lower latency, but requires careful memory management and has a less conventional API.

## Testing

### C++ Tests

| Tool | Purpose | Integration |
|------|---------|-------------|
| GoogleTest | Unit and integration tests | CTest runner |
| GoogleBenchmark | Performance regression tests | CTest + CI comparison |
| CTest | Test orchestration | CMake native |

### Image Quality Assessment

| Tool | Purpose |
|------|---------|
| IQA-PyTorch | FR metrics (PSNR, SSIM, LPIPS), NR metrics (NIQE, MUSIQ) |

Python toolchain in `tools/iqa/`. Runs as CI step comparing pipeline output against reference images. See [isp.md](isp.md) for evaluation protocol.

### Frontend Tests (M3)

| Tool | Purpose |
|------|---------|
| Vitest | Unit tests for React components |
| Playwright | E2E tests for pipeline editor |

### Android Tests (M5)

| Tool | Purpose |
|------|---------|
| AndroidX Test | Instrumentation tests on device/emulator |

## Platform Support Matrix

| Platform | Compiler | GPU Backend | AI Inference | Buffer Backend | DNG Reader | HEIF Writer |
|----------|----------|-------------|-------------|----------------|------------|-------------|
| Linux | GCC 13+ / Clang 15+ | Vulkan | ONNX Runtime (CUDA/CPU) | Vulkan Buffer / Host | libraw | libheif |
| macOS | Clang 15+ (Xcode) | Metal | ONNX Runtime (CoreML/CPU) | Metal Buffer / Host | libraw | libheif |
| Windows | MSVC 2022 / Clang | Vulkan | ONNX Runtime (CUDA/DirectML) | Vulkan Buffer / Host | libraw | libheif |
| Android | NDK r26+ (Clang) | Vulkan | ExecuTorch (QNN/XNNPACK) | AHardwareBuffer | Platform API | MediaCodec |
| iOS | Clang (Xcode) | Metal | ExecuTorch (CoreML/XNNPACK) | Metal Buffer | Platform API | Platform API |
