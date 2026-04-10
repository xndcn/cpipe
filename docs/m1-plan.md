# M1: Foundation -- Implementation Plan

## Overview

M1 builds the core infrastructure upon which all subsequent milestones depend: platform abstraction layer, buffer management, compute backend integration, plugin system, and format I/O. The goal is to establish a solid, well-tested foundation so that M2 can immediately begin writing ISP nodes and the pipeline engine.

**Target Version**: 0.1.0-alpha
**Dependencies**: M0 (COMPLETE)

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Target platforms | Desktop only (Linux + macOS); Android/iOS abstract interfaces only, no implementation | Android NDK cross-compilation is significant work; no pipeline engine to validate Android backend; keep M1 focused |
| Buffer backend | Host memory only (64-byte aligned heap); Vulkan/Metal backends deferred to M2+ | Halide manages host-to-GPU transfers; abstract interface is ready for future backends |
| Halide scope | CMake integration + HalideContext + buffer bridge + minimal test generator; no ISP generators | ISP nodes are M2 scope; M1 proves the Halide toolchain works end-to-end |
| Halide integration | Pre-built binary releases (FetchContent download) | Avoids 15+ min LLVM source build in CI; official packages support Linux-x86_64, macOS-arm64 |
| C ABI interface | Full architecture.md specification (all 7 functions) | Core contract -- design once, hard to change later; all functions are needed by M2-M3 |
| host_api_t callbacks | log + buffer_allocate/buffer_release + api_version | Sufficient for M2-M4; keeps interface minimal |
| Plugin loader | Directory scanning + explicit file loading | Needed for M2's `list-plugins` CLI command |
| DNG reader | Full metadata extraction (BlackLevel, ColorMatrix, CFAPattern, OpcodeList, NoiseProfile, etc.) | All metadata needed by M2 ISP nodes; libraw provides them with minimal extra work |
| HEIF writer | 8-bit HEIC (x265) + DNG EXIF passthrough + pipeline metadata embedding | EXIF preserves camera info; pipeline metadata enables traceability |
| Error handling | tl::expected polyfill with cpipe::Error type; cpipe_status_t at C ABI boundary | Type-safe errors in C++; clean C ABI boundary |
| Pixel format naming | RGB_FLOAT32 (not bare FLOAT32) | Explicit about channel layout |
| Test DNG source | Open-source reference DNG files via Git LFS | Real camera data for accurate testing |
| CI updates | Add Halide/vcpkg binary caching + Git LFS checkout | Prevent CI from becoming prohibitively slow |

## Architecture

### Dependency Graph

```
include/cpipe/types.h          (C types: status, pixel_format, device_type, buffer, node_info)
include/cpipe/version.h        (existing from M0)
        │
        ▼
src/common/                     (cpipe_common: error types, spdlog init, JSON utils)
        │
        ▼
src/platform/common/            (BufferPool, BufferDescriptor, DngReader interface, HeifWriter interface)
        │
        ├──────────────────────────────┐
        ▼                              ▼
src/platform/linux/             src/platform/apple/
  libraw_dng_reader               libraw_dng_reader (same impl, shared)
  libheif_writer                  libheif_writer (same impl, shared)
  host_buffer_backend             host_buffer_backend
        │
        ▼
include/cpipe/node_plugin.h     (C ABI: 7 exported functions)
include/cpipe/buffer.h          (BufferPool public API)
        │
        ▼
src/plugin/                     (PluginLoader + PluginRegistry)
        │
        ▼
src/compute/halide/             (HalideContext + Halide ↔ BufferPool bridge)
```

### CMake Target Graph

```
cpipe_common (STATIC)
    ├── depends: spdlog, nlohmann-json, tl-expected
    └── provides: error types, logging, JSON utils, version

cpipe_platform (STATIC)
    ├── depends: cpipe_common, libraw, libheif
    └── provides: BufferPool, BufferDescriptor, DngReader, HeifWriter

cpipe_plugin (STATIC)
    ├── depends: cpipe_common, cpipe_platform
    └── provides: PluginLoader, PluginRegistry

cpipe_compute (STATIC)
    ├── depends: cpipe_common, cpipe_platform, Halide
    └── provides: HalideContext, HalideBufferBridge
```

### New vcpkg Dependencies

| Package | Purpose | Notes |
|---------|---------|-------|
| nlohmann-json | JSON parsing/serialization | Pipeline JSON, plugin parameter schemas |
| tl-expected | std::expected polyfill | C++ error handling |
| libraw | DNG/RAW parsing | Desktop DNG reader backend |
| libheif | HEIF encoding | Desktop HEIF writer backend |

### New FetchContent Dependencies

| Package | Version | Source | Notes |
|---------|---------|--------|-------|
| Halide | v21.0.0 | GitHub Release pre-built binary | Platform-specific download URL per OS/arch |

## Public Header Specifications

### include/cpipe/types.h (C-compatible)

```c
/* Error codes */
typedef enum cpipe_status_t {
    CPIPE_STATUS_OK = 0,
    CPIPE_STATUS_ERROR_INVALID_PARAM,
    CPIPE_STATUS_ERROR_OUT_OF_MEMORY,
    CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
    CPIPE_STATUS_ERROR_IO,
    CPIPE_STATUS_ERROR_UNSUPPORTED,
    CPIPE_STATUS_ERROR_ABI_MISMATCH,
} cpipe_status_t;

/* Pixel formats */
typedef enum cpipe_pixel_format_t {
    CPIPE_PIXEL_FORMAT_BAYER_RGGB_16,
    CPIPE_PIXEL_FORMAT_BAYER_BGGR_16,
    CPIPE_PIXEL_FORMAT_BAYER_GRBG_16,
    CPIPE_PIXEL_FORMAT_BAYER_GBRG_16,
    CPIPE_PIXEL_FORMAT_RGB_16,
    CPIPE_PIXEL_FORMAT_RGB_8,
    CPIPE_PIXEL_FORMAT_RGBA_8,
    CPIPE_PIXEL_FORMAT_RGB_FLOAT32,
} cpipe_pixel_format_t;

/* Device types */
typedef enum cpipe_device_type_t {
    CPIPE_DEVICE_CPU  = 0,
    CPIPE_DEVICE_GPU  = 1,
    CPIPE_DEVICE_NPU  = 2,
} cpipe_device_type_t;

/* Buffer descriptor (C-compatible) */
typedef struct cpipe_buffer_t {
    uint32_t              width;
    uint32_t              height;
    cpipe_pixel_format_t  format;
    uint32_t              stride;     /* bytes per row, includes padding */
    cpipe_device_type_t   device;
    void*                 data;       /* opaque pointer to pixel data */
    uint64_t              size;       /* total bytes */
} cpipe_buffer_t;

/* Node metadata */
typedef struct cpipe_node_info_t {
    const char*           plugin_id;        /* e.g., "cpipe.isp.demosaic" */
    const char*           display_name;     /* e.g., "Demosaic (Malvar)" */
    const char*           version;          /* semver, e.g., "1.0.0" */
    uint32_t              abi_version;      /* must match host ABI version */
    uint32_t              input_count;
    uint32_t              output_count;
    uint32_t              supported_devices; /* bitmask of cpipe_device_type_t */
    const char*           category;         /* e.g., "isp.preprocessing" */
} cpipe_node_info_t;
```

### include/cpipe/node_plugin.h (C ABI)

```c
/* Host API callbacks provided to plugins */
typedef struct cpipe_host_api_t {
    uint32_t api_version;

    /* Logging */
    void (*log)(void* ctx, int level, const char* message);
    void* log_ctx;

    /* Buffer management */
    cpipe_status_t (*buffer_allocate)(void* ctx, cpipe_buffer_t* buf,
                                      uint32_t width, uint32_t height,
                                      cpipe_pixel_format_t format);
    void (*buffer_release)(void* ctx, cpipe_buffer_t* buf);
    void* buffer_ctx;
} cpipe_host_api_t;

/* Plugin lifecycle */
cpipe_status_t cpipe_plugin_init(const cpipe_host_api_t* host);
void           cpipe_plugin_shutdown(void);

/* Node lifecycle */
cpipe_node_t*  cpipe_node_create(const char* config_json);
void           cpipe_node_destroy(cpipe_node_t* node);

/* Node metadata */
const cpipe_node_info_t* cpipe_node_get_info(const cpipe_node_t* node);
const char*              cpipe_node_get_parameter_schema(const cpipe_node_t* node);

/* Node execution */
cpipe_status_t cpipe_node_process(
    cpipe_node_t* node,
    const cpipe_buffer_t* const* inputs,  uint32_t input_count,
    cpipe_buffer_t* const* outputs,       uint32_t output_count,
    const char* params_json
);
```

### include/cpipe/buffer.h (C++ public API)

C++ RAII wrapper around BufferPool:
- `BufferDescriptor` -- immutable buffer properties
- `Buffer` -- RAII handle to a pooled buffer (shared_ptr-based ref counting)
- `BufferPool` -- allocate/release with backend abstraction
- Conversion functions between `Buffer` and `cpipe_buffer_t`

## Phase Breakdown

### Phase 1: Foundation Infrastructure

**Scope**: Core types, error handling, logging, buffer management.

#### Task 1.1: Public Headers (types.h update + buffer.h)

- **Files**: `include/cpipe/types.h`, `include/cpipe/buffer.h`
- **Description**: Define all C-compatible types in `types.h`. Define C++ BufferPool/BufferDescriptor public API in `buffer.h`.
- **Acceptance criteria**:
  - [x] All types from the specification above are defined
  - [x] types.h compiles in both C and C++ modes
  - [x] buffer.h provides BufferDescriptor, Buffer, BufferPool declarations

#### Task 1.2: cpipe_common Library (expand from cpipe_core)

- **Files**: `src/common/CMakeLists.txt`, `src/common/error.h`, `src/common/error.cpp`, `src/common/log.h`, `src/common/log.cpp`, `src/common/json_utils.h`, `src/common/json_utils.cpp`
- **Description**: Rename cpipe_core → cpipe_common. Add Error type (cpipe_status_t + message), spdlog initialization, JSON helper utilities. Add tl-expected and nlohmann-json dependencies.
- **Acceptance criteria**:
  - [x] `cpipe::Error` type with status code and message
  - [x] `cpipe::expected<T, Error>` alias working (tl::expected or std::expected)
  - [x] spdlog initialization with configurable level
  - [x] JSON parse/serialize helpers
  - [x] Existing version tests still pass
  - [x] Build succeeds on Linux + macOS

#### Task 1.3: BufferPool + BufferDescriptor (Host Memory Backend)

- **Files**: `src/platform/common/buffer_pool.h`, `src/platform/common/buffer_pool.cpp`, `src/platform/common/buffer_descriptor.h`, `src/platform/CMakeLists.txt`
- **Description**: Implement BufferPool with host memory backend. 64-byte aligned allocation. Reference-counted buffers via shared_ptr. Pool reuses freed buffers when matching descriptor is requested.
- **Acceptance criteria**:
  - [x] Allocate buffers with specified width/height/format
  - [x] Correct stride calculation with alignment
  - [x] Reference counting: buffer returns to pool when last reference drops
  - [x] Pool reuse: freed buffer descriptor match → reuse memory
  - [x] Edge cases: zero-size request returns error, oversized request handled
  - [x] Thread safety: concurrent allocate/release

#### Task 1.4: Phase 1 Unit Tests

- **Files**: `tests/unit/common/error_test.cpp`, `tests/unit/platform/buffer_pool_test.cpp`
- **Description**: Comprehensive tests for Error type and BufferPool lifecycle.
- **Acceptance criteria**:
  - [x] Error type construction, status code extraction, message formatting
  - [x] BufferPool: allocate → use → release → reuse cycle
  - [x] BufferPool: reference counting correctness
  - [x] BufferPool: alignment verification (64-byte)
  - [x] BufferPool: boundary conditions
  - [x] All tests pass on Linux + macOS

**Checkpoint 1**: ✅ `cmake --build --preset default && ctest --preset default` passes. 54 tests green (including BufferPool lifecycle, overflow guards, thread safety).

---

### Phase 2: Plugin System

**Scope**: C ABI interface header, plugin loader, plugin registry, mock plugin for testing.

#### Task 2.1: node_plugin.h C ABI Header

- **Files**: `include/cpipe/node_plugin.h`
- **Description**: Define the complete C ABI plugin interface as specified above. Include cpipe_host_api_t, cpipe_node_t (opaque type), and all 7 exported function declarations. Header must be pure C (no C++ constructs).
- **Acceptance criteria**:
  - [ ] Compiles as C (tested with `gcc -std=c11`)
  - [ ] Compiles as C++ (tested with `g++ -std=c++20`)
  - [ ] All function signatures match architecture.md specification
  - [ ] cpipe_host_api_t includes log, buffer_allocate/release, api_version
  - [ ] Include guards via `#pragma once` + extern "C" for C++

#### Task 2.2: PluginLoader

- **Files**: `src/plugin/plugin_loader.h`, `src/plugin/plugin_loader.cpp`, `src/plugin/CMakeLists.txt`
- **Description**: Implement dynamic library loading wrapper. Supports directory scanning (load all .so/.dylib in a directory) and explicit file loading. Loading flow: dlopen → dlsym(cpipe_plugin_init) → call init(host_api) → check abi_version → dlsym remaining functions → return PluginHandle.
- **Acceptance criteria**:
  - [ ] Load a single plugin by file path
  - [ ] Scan a directory and load all plugins found
  - [ ] ABI version mismatch: reject plugin, log warning, continue
  - [ ] Invalid file (not a shared library): log warning, skip
  - [ ] Missing required symbols: log warning, skip
  - [ ] Return structured PluginHandle with function pointers
  - [ ] Platform abstraction: dlopen on Linux/macOS (LoadLibrary stub for future Windows)

#### Task 2.3: PluginRegistry

- **Files**: `src/plugin/plugin_registry.h`, `src/plugin/plugin_registry.cpp`
- **Description**: In-memory registry mapping plugin_id → PluginHandle. Supports registration, lookup by ID, and iteration (for list-plugins).
- **Acceptance criteria**:
  - [ ] Register a loaded plugin by plugin_id
  - [ ] Lookup by plugin_id returns PluginHandle or error
  - [ ] Iterate all registered plugins
  - [ ] Duplicate plugin_id: reject with warning (first-registered wins)

#### Task 2.4: Mock Plugin + Plugin System Tests

- **Files**: `tests/fixtures/mock_plugin/CMakeLists.txt`, `tests/fixtures/mock_plugin/mock_plugin.c`, `tests/unit/plugin/plugin_loader_test.cpp`, `tests/unit/plugin/plugin_registry_test.cpp`
- **Description**: Build a minimal mock plugin as a shared library (MODULE target in CMake). The mock plugin implements all C ABI functions with trivial logic (e.g., process copies input to output). Tests verify the full load → register → lookup → create → process → destroy → shutdown cycle.
- **Acceptance criteria**:
  - [ ] Mock plugin compiles as MODULE (shared library)
  - [ ] Mock plugin implements all 7 C ABI functions
  - [ ] PluginLoader test: load mock plugin, verify all function pointers
  - [ ] PluginLoader test: reject ABI-mismatched plugin (build a second mock with wrong version)
  - [ ] PluginLoader test: directory scan finds mock plugin
  - [ ] PluginRegistry test: register, lookup, iterate
  - [ ] Full cycle test: load → register → create node → process → destroy → shutdown

**Checkpoint 2**: All Phase 1 + Phase 2 tests pass. Mock plugin loads, registers, and executes correctly.

---

### Phase 3: Format I/O + Halide Integration

**Scope**: DNG reading, HEIF writing, Halide runtime integration.

#### Task 3.1: DngReader Abstract Interface + libraw Backend

- **Files**: `src/platform/common/dng_reader.h`, `src/platform/linux/libraw_dng_reader.h`, `src/platform/linux/libraw_dng_reader.cpp`
- **Description**: Define abstract DngReader interface. Implement libraw backend for desktop. Extract Bayer pixel data into BufferPool-allocated buffer. Extract full metadata: BlackLevel, BlackLevelDeltaH/V, CFAPattern, ColorMatrix1/2, ForwardMatrix1/2, OpcodeList3 (LSC gain maps), NoiseProfile, WhiteLevel, AsShotNeutral.
- **Acceptance criteria**:
  - [ ] Open DNG file, read Bayer data into Buffer
  - [ ] Correct pixel format detection (RGGB/BGGR/GRBG/GBRG)
  - [ ] BlackLevel extraction (per-channel, with delta support)
  - [ ] ColorMatrix1/2 extraction with illuminant info
  - [ ] ForwardMatrix1/2 extraction
  - [ ] CFAPattern detection
  - [ ] NoiseProfile extraction
  - [ ] WhiteLevel extraction
  - [ ] AsShotNeutral / AsShotWhiteXY extraction
  - [ ] Error handling: invalid file, corrupt data, missing fields
  - [ ] Metadata returned as structured DngMetadata object

#### Task 3.2: HeifWriter Abstract Interface + libheif Backend

- **Files**: `src/platform/common/heif_writer.h`, `src/platform/linux/libheif_writer.h`, `src/platform/linux/libheif_writer.cpp`
- **Description**: Define abstract HeifWriter interface. Implement libheif backend. Accept RGB 8-bit buffer, encode as HEIC (x265). Support configurable compression quality. Passthrough DNG EXIF fields (Make, Model, DateTime, ExposureTime, FNumber, ISO, FocalLength, GPS). Embed cpipe pipeline metadata in XMP or UserComment.
- **Acceptance criteria**:
  - [ ] Write 8-bit RGB buffer to HEIC file
  - [ ] Configurable compression quality (0-100)
  - [ ] DNG EXIF passthrough: Make, Model, DateTime, ExposureTime, FNumber, ISOSpeedRatings, FocalLength, GPS
  - [ ] Pipeline metadata embedding (cpipe version, pipeline name, processing parameters summary)
  - [ ] Output file is valid HEIF (readable by libheif/system tools)
  - [ ] Error handling: write failure, encoder error

#### Task 3.3: Halide CMake Integration

- **Files**: `CMakeLists.txt` (root, add Halide FetchContent), `src/compute/CMakeLists.txt`
- **Description**: Integrate Halide v21.0 pre-built binary via FetchContent. Configure platform-appropriate download (Linux-x86_64, macOS-arm64). Verify `find_package(Halide)` works and `add_halide_library()` is available.
- **Acceptance criteria**:
  - [ ] Halide v21.0 pre-built binary downloaded and found
  - [ ] `find_package(Halide REQUIRED)` succeeds
  - [ ] `add_halide_library()` CMake function available
  - [ ] Works on both Linux (GCC) and macOS (Apple Clang)

#### Task 3.4: HalideContext + Buffer Bridge

- **Files**: `src/compute/halide/halide_context.h`, `src/compute/halide/halide_context.cpp`, `src/compute/halide/halide_buffer_bridge.h`, `src/compute/halide/halide_buffer_bridge.cpp`
- **Description**: HalideContext manages Halide target selection (host CPU for M1; GPU targets registered but not activated until Vulkan/Metal backends are implemented). HalideBufferBridge provides zero-copy conversion between cpipe Buffer and Halide::Buffer<>.
- **Acceptance criteria**:
  - [ ] HalideContext selects host target by default
  - [ ] HalideContext can list available targets
  - [ ] Bridge: cpipe Buffer → Halide::Buffer<> (zero-copy, wraps same memory)
  - [ ] Bridge: Halide::Buffer<> → cpipe Buffer (zero-copy)
  - [ ] Correct stride/dimension mapping for Bayer and RGB formats

#### Task 3.5: Minimal Halide Test Generator

- **Files**: `halide/CMakeLists.txt`, `halide/test_generator.cpp`
- **Description**: Write a trivial Halide generator (e.g., pixel-wise multiply by constant) to validate the full toolchain: generator builds → AOT compilation → linked into test → executes correctly on buffer from BufferPool via bridge.
- **Acceptance criteria**:
  - [ ] Generator compiles and produces AOT kernel
  - [ ] Kernel links into test executable
  - [ ] Kernel executes on BufferPool-allocated buffer via bridge
  - [ ] Output values are mathematically correct
  - [ ] Works on both Linux and macOS

#### Task 3.6: Phase 3 Unit Tests

- **Files**: `tests/unit/platform/dng_reader_test.cpp`, `tests/unit/platform/heif_writer_test.cpp`, `tests/unit/compute/halide_context_test.cpp`, `tests/unit/compute/halide_bridge_test.cpp`
- **Description**: Tests using real reference DNG files and round-trip HEIF verification.
- **Acceptance criteria**:
  - [ ] DNG reader: open reference DNG, verify dimensions, CFA pattern, metadata fields
  - [ ] DNG reader: verify pixel data non-zero and within expected range
  - [ ] HEIF writer: write test buffer → read back → verify dimensions and basic pixel values
  - [ ] HEIF writer: verify EXIF fields present in output
  - [ ] HalideContext: target selection test
  - [ ] Buffer bridge: round-trip conversion preserves data
  - [ ] Test generator: AOT kernel produces correct output

#### Task 3.7: Git LFS Setup + Test Fixtures

- **Files**: `.gitattributes`, `tests/fixtures/images/` (DNG files)
- **Description**: Configure Git LFS for DNG test files. Add 1-2 reference DNG files (small, <5MB each) covering RGGB and BGGR CFA patterns. Update CI to checkout LFS files.
- **Acceptance criteria**:
  - [ ] `.gitattributes` tracks `*.dng` files via LFS
  - [ ] At least 2 reference DNG files present (RGGB + BGGR)
  - [ ] CI workflow checks out LFS files before test step

**Checkpoint 3**: All M1 tests pass. DNG → Buffer → HEIF round-trip works. Halide test generator executes correctly.

---

### Phase 4: CI + Documentation + Finalization

#### Task 4.1: CI/CD Updates

- **Files**: `.github/workflows/ci.yml`
- **Description**: Update CI for M1 dependencies and test requirements.
- **Acceptance criteria**:
  - [ ] Halide pre-built binary cached between runs
  - [ ] vcpkg binary cache configured (GitHub Actions cache or NuGet)
  - [ ] Git LFS checkout in CI workflow
  - [ ] All M1 tests pass on Linux (ubuntu-24.04, GCC) and macOS (macos-14, Apple Clang)
  - [ ] CI total time < 15 minutes

#### Task 4.2: Documentation Updates

- **Files**: `docs/roadmap.md`, `CHANGELOG.md`, `docs/architecture.md` (if needed)
- **Description**: Update roadmap M1 status, add CHANGELOG entries, update architecture.md if any design changes occurred during implementation.
- **Acceptance criteria**:
  - [ ] roadmap.md: M1 status → COMPLETE with date and notes
  - [ ] CHANGELOG.md: all M1 additions listed under appropriate version
  - [ ] architecture.md: updated if any interface or structure diverged from plan

## File Manifest

New files created in M1 (excluding test files):

```
include/cpipe/
    types.h                         (NEW: C-compatible type definitions)
    buffer.h                        (NEW: BufferPool/BufferDescriptor C++ API)
    node_plugin.h                   (NEW: C ABI plugin interface)

src/common/
    error.h / error.cpp             (NEW: Error type, expected alias)
    log.h / log.cpp                 (NEW: spdlog initialization)
    json_utils.h / json_utils.cpp   (NEW: JSON parsing helpers)

src/platform/
    CMakeLists.txt                  (NEW: cpipe_platform target)
    common/
        buffer_pool.h / .cpp        (NEW: BufferPool with host memory backend)
        buffer_descriptor.h         (NEW: Immutable buffer descriptor)
        dng_reader.h                (NEW: Abstract DNG reader interface)
        heif_writer.h               (NEW: Abstract HEIF writer interface)
    linux/
        host_buffer_backend.h / .cpp    (NEW: Host memory buffer backend)
        libraw_dng_reader.h / .cpp      (NEW: libraw DNG reader implementation)
        libheif_writer.h / .cpp         (NEW: libheif HEIF writer implementation)

src/compute/
    CMakeLists.txt                  (NEW: cpipe_compute target)
    halide/
        halide_context.h / .cpp     (NEW: Halide target management)
        halide_buffer_bridge.h / .cpp (NEW: cpipe Buffer ↔ Halide::Buffer bridge)

src/plugin/
    CMakeLists.txt                  (NEW: cpipe_plugin target)
    plugin_loader.h / .cpp          (NEW: dlopen/LoadLibrary wrapper)
    plugin_registry.h / .cpp        (NEW: Plugin ID → PluginHandle map)

halide/
    CMakeLists.txt                  (NEW: Halide generator build rules)
    test_generator.cpp              (NEW: Minimal test generator)
```

Modified files:

```
CMakeLists.txt                      (ADD: Halide FetchContent, new subdirectories)
vcpkg.json                          (ADD: nlohmann-json, tl-expected, libraw, libheif)
src/common/CMakeLists.txt           (MODIFY: rename cpipe_core → cpipe_common, add sources)
tests/CMakeLists.txt                (MODIFY: add new test subdirectories)
.github/workflows/ci.yml           (MODIFY: add caching, Git LFS)
.gitattributes                      (NEW: Git LFS tracking rules)
docs/roadmap.md                     (MODIFY: M1 status update)
CHANGELOG.md                        (MODIFY: M1 entries)
```

## Definition of Done

M1 is COMPLETE when all of the following are true:

1. **Public headers**: `types.h`, `buffer.h`, `node_plugin.h`, `version.h` are all in place and compile in both C and C++ modes (where applicable)
2. **BufferPool**: Can allocate, release, and reuse host memory buffers with correct reference counting
3. **Plugin system**: PluginLoader can load a mock plugin (.so/.dylib) and register it in PluginRegistry; full lifecycle (init → create → process → destroy → shutdown) works
4. **DNG reader**: Reads reference DNG files, extracts Bayer data + full metadata (BlackLevel, ColorMatrix, CFAPattern, OpcodeList, NoiseProfile, etc.)
5. **HEIF writer**: Writes 8-bit HEIC files with DNG EXIF passthrough and cpipe pipeline metadata
6. **Halide integration**: HalideContext selects target; buffer bridge converts between cpipe Buffer and Halide::Buffer; minimal test generator AOT-compiles and executes correctly
7. **Test coverage**: ≥80% for new code
8. **CI**: All tests pass on Linux (ubuntu-24.04, GCC) and macOS (macos-14, Apple Clang) with Halide/vcpkg caching and Git LFS
9. **Documentation**: roadmap.md updated, CHANGELOG.md updated, architecture.md updated if needed

## Risks and Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Halide pre-built binary incompatible with CI environment | High | Low | Fall back to source build with aggressive caching; pin exact Halide release |
| libraw doesn't expose all needed DNG metadata fields | Medium | Low | Check libraw API docs upfront; OpcodeList3 parsing may need custom code |
| libheif EXIF embedding API complexity | Medium | Medium | Start with basic EXIF; use libheif's heif_context_add_exif_data(); test incrementally |
| Mock plugin ABI testing fragile across compilers | Medium | Medium | Use C-only mock plugin; test on both GCC and Clang in CI |
| BufferPool thread safety issues | High | Medium | Use mutex-based locking initially; benchmark later; test with thread sanitizer |
| Halide Buffer bridge stride mismatch | Medium | Medium | Extensive unit tests for all supported pixel formats; verify with real DNG dimensions |

## Open Questions

_None -- all decisions resolved during planning._
