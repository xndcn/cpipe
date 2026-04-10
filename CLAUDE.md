# cpipe -- Agent Development Guide

## Project Overview

cpipe is a computational photography pipeline that processes RAW images through a DAG of plugin nodes. See [docs/architecture.md](docs/architecture.md) for the full system design and [docs/roadmap.md](docs/roadmap.md) for the development plan.

## Build Commands

```bash
cmake --preset default            # Configure
cmake --build --preset default    # Build
ctest --preset default            # Test
```

## Code Conventions

### Language

- **C++20** standard. Use concepts, designated initializers, `std::span`, `std::format`.
- Do NOT use C++23 features without feature-test macro guards (e.g., `__cpp_lib_expected`). Provide polyfills (e.g., `tl::expected`) where needed.

### Style

- **clang-format** enforced. PostToolUse hook auto-formats `.cpp`/`.h` files on save.
- **Naming**:
  - `snake_case` for functions, variables, namespaces, file names
  - `PascalCase` for types (classes, structs, enums, concepts)
  - `UPPER_SNAKE_CASE` for constants and macros
  - `k` prefix NOT used (use `UPPER_SNAKE_CASE` instead)
- **Namespaces**: `cpipe::` top-level
  - `cpipe::platform::` -- Platform layer (buffer, DNG, HEIF)
  - `cpipe::compute::` -- Compute backends (Halide, Vulkan, Metal, inference)
  - `cpipe::engine::` -- Pipeline engine (loader, scheduler, profiler)
  - `cpipe::plugin::` -- Plugin system (loader, registry)
- **Header guards**: `#pragma once` (no include guards)
- **Includes**: Sorted, grouped by category, separated by blank lines:
  1. System headers (`<string>`, `<vector>`)
  2. Third-party headers (`<spdlog/spdlog.h>`, `<nlohmann/json.hpp>`)
  3. Project headers (`<cpipe/types.h>`, `"internal_header.h"`)

### File Organization

- Public headers in `include/cpipe/` (installed with the library)
- Internal headers alongside their `.cpp` files
- One class per file, file named after class (`buffer_pool.h` → `BufferPool`)
- **Max 400 lines per file**, 800 absolute limit. Extract utilities when approaching limit.
- Plugin interface headers (`node_plugin.h`) are **C-only** -- no C++ constructs.

### Error Handling

- Use `std::expected<T, Error>` for fallible functions (with polyfill for compilers without C++23 support)
- **Never throw exceptions across plugin boundaries** (C ABI)
- Plugin ABI uses error codes (`cpipe_status_t` enum)
- Log errors via spdlog before returning error codes
- Do NOT silently swallow errors

### Immutability

- `BufferDescriptor` is immutable after creation
- `Pipeline` definitions are immutable once loaded
- Node parameters use copy-on-write semantics when modified at runtime
- **Never mutate shared buffers** -- always allocate new buffers from the pool

### Memory

- **All image buffers** must be allocated from `BufferPool` -- no raw `new`/`malloc` for image data
- RAII for all resources (no manual `new`/`delete`)
- Prefer `std::unique_ptr` (unique ownership) over `std::shared_ptr`
- Use `std::shared_ptr` only where shared ownership is genuinely needed (e.g., buffer reference counting)
- `std::weak_ptr` for observers

## Architecture Rules

### Plugin System

- ALL nodes are plugins, including built-in ISP algorithms
- Plugins expose **C ABI only** (`node_plugin.h`)
- Plugins are loaded via `dlopen`/`LoadLibrary` at runtime
- Plugin internal implementation may use C++ freely
- See [docs/architecture.md](docs/architecture.md) for the C ABI interface specification

### Buffer Management

- Never allocate raw memory for image data -- always use `BufferPool::allocate()`
- Buffers are reference-counted, automatically returned to pool when no longer referenced
- Platform-specific backends handle CPU/GPU mapping transparently
- Prefer keeping data on-device (avoid CPU↔GPU transfers where possible)

### Pipeline

- Pipeline topology defined in JSON, validated against JSON Schema before execution
- DAG scheduling via Taskflow -- do NOT manually manage thread pools or spawn `std::thread`
- `DeviceAllocator` decides CPU vs GPU vs NPU per node

### Compute

- Default to Halide for all ISP node implementations
- Native Vulkan/Metal compute shaders only when profiling shows >20% improvement
- AI nodes use the `InferenceBackend` abstraction -- never call ExecuTorch or ONNX Runtime directly from node code

## Testing Requirements

- **Minimum 80% code coverage** for new code
- **Unit tests**: GoogleTest, one test file per source file in `tests/unit/`
- **Integration tests**: Full pipeline execution on reference DNG images in `tests/integration/`
- **Benchmarks**: GoogleBenchmark for performance-critical paths in `tests/benchmark/`
- **Test naming**: `TEST(ClassName, MethodName_Condition_ExpectedBehavior)`
- **IQA evaluation**: Python toolchain in `tools/iqa/` for image quality metrics
- **Android**: Instrumentation tests for Camera2 integration and end-to-end capture

## Document Update Requirements

**MANDATORY** after completing any work:

| When | Update |
|------|--------|
| Milestone work completed | `docs/roadmap.md` -- change milestone status, add completion notes |
| Any change | `CHANGELOG.md` -- add entry under appropriate version heading |
| Architecture changes | `docs/architecture.md` -- update diagrams and specifications |
| New dependency added | `docs/tech.md` -- add entry with rationale and version |
| ISP node added or changed | `docs/isp.md` -- update node specification and references |

### CHANGELOG Format

```markdown
## [version] - YYYY-MM-DD

### Added
- Description of new feature

### Changed
- Description of change

### Fixed
- Description of bug fix
```

## Commit Conventions

- **Format**: `<type>: <description>`
- **Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`
- **Examples**:
  - `feat: add BLC node plugin with Halide implementation`
  - `fix: correct buffer stride calculation for odd widths`
  - `docs: update roadmap after M1 completion`
  - `test: add integration test for demosaic node`
  - `perf: optimize LSC gain map interpolation with Halide schedule`

## Directory Map

| Path | Contents |
|------|----------|
| `include/cpipe/` | Public C/C++ headers (plugin API, buffer, types) -- minimal surface |
| `src/common/` | Cross-layer shared utilities: error types, logging, JSON helpers |
| `src/platform/` | Platform layer: buffer pool, DNG reader, HEIF writer |
| `src/platform/common/` | Abstract interfaces + platform-agnostic code |
| `src/platform/{linux,android,apple}/` | Platform-specific implementations |
| `src/compute/` | Compute backends: Halide, Vulkan, Metal, AI inference |
| `src/compute/inference/{executorch,onnxruntime}/` | AI inference backend implementations |
| `src/engine/` | Pipeline engine: JSON loader, DAG scheduler, profiler |
| `src/plugin/` | Plugin system: dynamic loader, registry |
| `src/cli/` | CLI application (main.cpp, subcommands) |
| `halide/` | Halide AOT generators (host-side executables, build phase isolation) |
| `plugins/` | Built-in node plugins (each a self-contained shared library) |
| `plugins/isp/` | Classical ISP nodes (Halide-based): blc, lsc, demosaic, etc. |
| `plugins/ai/` | AI model nodes (M4): denoise, awb, nilut |
| `plugins/io/` | Utility/IO nodes (future) |
| `tests/unit/` | GoogleTest unit tests (mirrors src/ structure) |
| `tests/integration/` | Full pipeline integration tests |
| `tests/benchmark/` | GoogleBenchmark performance tests |
| `tests/fixtures/` | Test data: reference images, pipeline JSON, expected outputs |
| `examples/pipelines/` | Sample pipeline JSON files (also used as integration test inputs) |
| `tools/iqa/` | Python IQA evaluation scripts |
| `schemas/` | JSON Schema for pipeline format |
| `editor/` | React Flow pipeline editor (M3, placeholder) |
| `android/` | Android app (M5, placeholder) |
| `docs/` | Architecture, tech selections, ISP reference, roadmap |
| `docs/solutions/` | Documented solutions to past problems and best practices, organized by category with YAML frontmatter (`module`, `tags`, `problem_type`) |

Note: Plugin tests are colocated within each plugin directory (e.g., `plugins/isp/blc/blc_test.cpp`).

## Key Dependencies

| Library | Purpose | Version | Source |
|---------|---------|---------|--------|
| Halide | GPU/CPU compute DSL | v21.0 | FetchContent |
| Taskflow | DAG scheduling | v4.0.0 | FetchContent |
| ExecuTorch | AI inference (mobile) | v1.2.0 | FetchContent |
| ONNX Runtime | AI inference (desktop) | v1.24.4 | FetchContent |
| spdlog | Logging | latest | vcpkg |
| nlohmann/json | JSON parsing | latest | vcpkg |
| CLI11 | CLI argument parsing | latest | vcpkg |
| GoogleTest | Unit testing | latest | vcpkg |
| GoogleBenchmark | Performance benchmarks | latest | vcpkg |
| libraw | DNG parsing (desktop) | v0.22.1 | vcpkg |
| libheif | HEIF encoding (desktop) | v1.21.2 | vcpkg |

## Common Pitfalls

- **No exceptions across C ABI**: Plugin boundary is C-only. Use `cpipe_status_t` error codes.
- **No raw memory for images**: Always use `BufferPool::allocate()`. Stack allocation of image buffers is forbidden.
- **No hardcoded Bayer pattern**: Always read CFA layout from DNG metadata or pipeline configuration.
- **No endianness assumptions**: Use explicit byte-order handling for file I/O.
- **No `std::thread`**: Use Taskflow for all parallelism. Manual threading breaks the DAG scheduler.
- **No model weights in git**: Use Git LFS or external storage for `.onnx`, `.pte`, `.tflite` files.
- **No `console.log` / `printf`**: Use spdlog for all logging. Debug prints must use `SPDLOG_DEBUG()`.
- **No mutable shared state**: Buffers are reference-counted and immutable. Node parameters use copy-on-write.
