# Roadmap

## Overview

cpipe follows a milestone-based development approach. Each milestone builds on the previous one, progressively adding capabilities from core infrastructure through to the full mobile application. Initial focus is on the desktop CLI and web editor; Android integration comes after the pipeline engine is stable.

## Milestones

### M0: Project Scaffolding + Documentation + CI/CD

- **Status**: COMPLETE (2026-04-10)
- **Goal**: Establish repository structure, documentation, and development conventions
- **Deliverables**:
  - Project documentation: README, architecture, tech selections, ISP reference, roadmap ✓
  - CLAUDE.md agent development guide ✓
  - Apache 2.0 license, .clang-format, .editorconfig, .gitignore ✓
  - CMake build system: `cpipe_core` static library, `CMakePresets.json` (Debug/Release), vcpkg manifest ✓
  - GitHub Actions CI: build matrix for Linux (ubuntu-24.04, GCC) and macOS (macos-14, Apple Clang) with clang-format check and artifact upload ✓
  - GoogleTest harness with CTest integration; 3 version tests passing ✓
- **Notes**: Windows CI deferred to a later milestone; Linux + macOS covers initial development
- **Dependencies**: None

### M1: Foundation

- **Status**: PLANNED
- **Goal**: Platform abstraction layer, compute backends, buffer management, and plugin interface
- **Deliverables**:
  - `BufferPool` + `BufferDescriptor` with platform-specific backends
    - Desktop: Vulkan buffer / host-allocated memory
    - Android: AHardwareBuffer (zero-copy with Camera2, GPU, NNAPI)
  - Halide v21.0 integration with Vulkan and Metal backend selection
  - C ABI plugin interface (`include/cpipe/node_plugin.h`)
  - Plugin loader: directory scanning, dlopen/LoadLibrary, version checking
  - Platform abstraction: DNG reader (libraw desktop, platform API Android)
  - Platform abstraction: HEIF writer (libheif desktop, MediaCodec Android)
  - Unit tests for buffer lifecycle, plugin loading, format I/O
- **Dependencies**: M0

### M2: PipelineEngine + DAG Scheduling + Basic ISP + CLI

- **Status**: PLANNED
- **Goal**: End-to-end RAW-to-HEIF processing via CLI
- **Deliverables**:
  - JSON pipeline loader with JSON Schema validation
  - Taskflow v4.0.0 DAG scheduler with custom device allocation layer
  - `DeviceAllocator`: assigns nodes to CPU/GPU/NPU based on hints and availability
  - MVP ISP nodes as plugins (all Halide-based):
    - RAW preprocessing: BLC, LSC, Bad Pixel Correction
    - Core ISP: Demosaic (Malvar-He-Cutler), AWB (Gray World), CCM, Gamma/Tone Curve
  - Profiler: per-node timing, memory high-water mark
  - CLI tool with subcommands: `process`, `list-plugins`, `inspect`, `benchmark`
  - Integration tests: full pipeline execution on reference DNG images
  - Performance benchmarks via GoogleBenchmark
- **Dependencies**: M1

### M3: Web Pipeline Editor

- **Status**: PLANNED
- **Goal**: Visual DAG editor communicating with cpipe via WebSocket
- **Deliverables**:
  - React Flow 12.x static site deployable on GitHub Pages
  - WebSocket server in `cpipe serve` (JSON-RPC protocol)
  - Pipeline JSON import/export with PNG metadata embedding
  - Real-time parameter tuning with node preview images
  - Per-node profiling display (execution time, memory usage)
  - Pipeline sharing: export as JSON file or PNG with embedded workflow
- **Dependencies**: M2

### M4: AI Inference Integration

- **Status**: PLANNED
- **Goal**: AI model nodes running alongside classical ISP nodes
- **Deliverables**:
  - Unified `InferenceBackend` abstraction with two implementations:
    - ExecuTorch backend (primary for mobile, NPU delegates)
    - ONNX Runtime backend (primary for desktop, CUDA/CoreML providers)
  - AI ISP nodes as plugins:
    - NAFNet-based RAW denoising
    - Learned AWB (time-aware, ~5K params)
    - NILUT neural color mapping
  - Model packaging and versioning scheme
  - Benchmark comparison: classical vs AI node variants (latency + IQA metrics)
- **Dependencies**: M2

### M5: Android Integration

- **Status**: PLANNED
- **Goal**: Camera2 capture with real-time pipeline preview on Android
- **Deliverables**:
  - Camera2 API integration (Kotlin/Java thin layer)
  - Real-time preview pipeline (downscaled DAG variant)
  - Full-resolution capture pipeline (triggered on shutter)
  - AHardwareBuffer zero-copy path: Camera2 → GPU → NPU → encoder
  - HEIF output via MediaCodec H.265 encoder + MediaStore
  - Android instrumentation tests
- **Dependencies**: M1, M4

### M6: Multi-frame Processing + HDR + Calibration

- **Status**: PLANNED
- **Goal**: Burst capture, HDR fusion, and device calibration workflow
- **Deliverables**:
  - Burst capture with frame alignment (coarse-to-fine optical flow)
  - Multi-frame fusion node (noise reduction via temporal averaging)
  - HDR merge + tone mapping node
  - Calibration data flow:
    1. DNG embedded metadata (ColorMatrix, ForwardMatrix, NoiseProfile)
    2. Custom device calibration config file (JSON, per-device)
    3. In-app calibration tool (color chart capture → CCM generation)
- **Dependencies**: M5

### M7: Community + Polish

- **Status**: PLANNED
- **Goal**: Plugin marketplace, performance optimization, comprehensive documentation
- **Deliverables**:
  - Plugin distribution format and community registry
  - Pipeline sharing platform integration
  - Performance optimization pass (profile-guided, native shader hotspots)
  - Comprehensive API documentation (Doxygen + usage guides)
  - Contributing guide and plugin development tutorial
  - iOS support finalization (Metal backend + CoreML inference)
- **Dependencies**: M6

## Version Mapping

| Milestone | Target Version | Focus |
|-----------|---------------|-------|
| M0 | 0.0.1-alpha | Scaffolding, documentation |
| M1 | 0.1.0-alpha | Core infrastructure |
| M2 | 0.2.0-alpha | Pipeline engine, CLI |
| M3 | 0.3.0-alpha | Web editor |
| M4 | 0.4.0-alpha | AI inference |
| M5 | 0.5.0-beta | Android app |
| M6 | 0.6.0-beta | Multi-frame, HDR |
| M7 | 1.0.0 | Production release |
