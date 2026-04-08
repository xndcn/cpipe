# System Architecture

## Overview

cpipe is a modular computational photography pipeline that processes RAW images through a DAG (Directed Acyclic Graph) of processing nodes. Every node -- whether a classical Halide algorithm or an AI model -- is a plugin loaded at runtime via a C ABI interface. The system is designed for cross-platform deployment (Linux, macOS, Windows, Android, iOS) with zero-copy buffer management and heterogeneous compute (CPU, GPU, NPU).

## Layer Diagram

```
+---------------------------------------------------------------------+
|                         Application Layer                           |
|  cpipe CLI (CLI11)  |  Android App (Kotlin)  |  Pipeline Editor    |
|  process / inspect  |  Camera2 + Preview     |  React Flow (Web)   |
|  benchmark / serve  |  HEIF capture          |  WebSocket JSON-RPC |
+---------------------------------------------------------------------+
|                        Pipeline Engine                              |
|  PipelineLoader     |  DagScheduler          |  Profiler           |
|  (JSON + Schema)    |  (Taskflow v4.0)       |  (per-node timing)  |
|                     |  DeviceAllocator       |                     |
|                     |  (CPU/GPU/NPU)         |                     |
+---------------------------------------------------------------------+
|                         Plugin System                               |
|  C ABI Interface (node_plugin.h)                                    |
|  PluginRegistry  |  DynamicLoader  |  ParameterSchema (JSON)       |
+---------------------------------------------------------------------+
|                       ISP Node Plugins                              |
|  BLC | LSC | Bad Pixel | Demosaic | AWB | CCM | Gamma | ...       |
|  (each is a .so / .dylib / .dll shared library)                    |
+---------------------------------------------------------------------+
|                        Compute Layer                                |
|  HalideContext       |  Native GPU           |  InferenceBackend   |
|  (Vulkan/Metal/CUDA) |  (Vulkan/Metal CS)    |  (ExecuTorch|ONNX) |
+---------------------------------------------------------------------+
|                        Platform Layer                               |
|  BufferPool          |  DngReader            |  HeifWriter         |
|  BufferDescriptor    |  (libraw | Platform)  |  (libheif | Media-  |
|  (AHardwareBuffer    |                       |   Codec)            |
|   | Vulkan | Host)   |                       |                     |
+---------------------------------------------------------------------+
|                      Operating System                               |
|  Linux  |  macOS  |  Windows  |  Android  |  iOS                   |
+---------------------------------------------------------------------+
```

## Data Flow

### Full Pipeline Execution

```
Input: DNG file (or Camera2 RAW frame)
                    |
                    v
            +---------------+
            |   DNG Reader  |    Platform Layer
            | (libraw/API)  |    Extracts: Bayer data, metadata
            +-------+-------+    (BlackLevel, ColorMatrix, CFA, etc.)
                    |
                    v
           RawBuffer (Bayer 16-bit, in BufferPool)
                    |
                    v
            +---------------+
            | PipelineLoader |   Engine Layer
            | (parse JSON,  |   Resolves plugin references
            |  build DAG)   |   Validates against JSON Schema
            +-------+-------+
                    |
                    v
            +---------------+
            | DagScheduler  |   Engine Layer
            | (Taskflow)    |   Topological sort
            +-------+-------+   Parallel branches where possible
                    |
     +--------------+--------------+
     |              |              |
     v              v              v
  [BLC]         [Bad Pixel]     (parallel if independent)
     |              |
     v              v
     +------+-------+
            |
            v
         [LSC]
            |
            v
       [Demosaic]     GPU preferred
            |
            v
         [AWB]
            |
            v
         [CCM]
            |
            v
        [Gamma]
            |
            v
    OutputBuffer (RGB 8/10-bit, in BufferPool)
            |
            v
      +-------------+
      | HEIF Writer |   Platform Layer
      | (libheif/   |   H.265/HEVC encoding
      |  MediaCodec)|   Metadata embedding
      +------+------+
             |
             v
       Output: .heif file
```

### Buffer Lifecycle

```
                   BufferPool
                  /          \
    allocate()   /            \   allocate()
                v              v
          BufferA            BufferB
          (ref=1)            (ref=1)
              |                  |
              v                  v
         [Node 1]           [Node 2]
         output: A           input: A (ref=2), output: B
              |                  |
              v                  v
         A passed to         B passed to
         Node 2 input        Node 3 input
         (ref count++)       (ref count++)
              |                  |
              v                  v
         Node 1 done         Node 2 done
         (A ref--)           (A ref--, B ref unchanged)
              |                  |
              v                  v
         A ref=1             A ref=0 → returned to pool
         (still used         B ref=1 (still used)
          by Node 2)

Key invariants:
- Buffers are NEVER copied between nodes (zero-copy)
- Buffers are reference-counted via shared_ptr
- When ref count reaches 0, buffer returns to pool
- Pool reuses memory without reallocation
- Platform backend handles GPU/CPU mapping transparently
```

## Layer Specifications

### Platform Layer

**Responsibility**: OS-specific I/O, memory management, and hardware buffer abstraction.

**Key types**:

- **BufferPool**: Pre-allocated memory pool. Manages a set of buffers with configurable capacity. Supports multiple device backends.
- **BufferDescriptor**: Immutable description of a buffer's properties.
- **DngReader**: Abstract interface for reading DNG/RAW files. Desktop: libraw. Android: platform `ImageReader` API.
- **HeifWriter**: Abstract interface for encoding and writing HEIF files. Desktop: libheif. Android: MediaCodec + MediaStore.

**Cross-reference**: [tech.md](tech.md) for library selections and platform support matrix.

### Compute Layer

**Responsibility**: Execute computation on CPU, GPU, or NPU.

**Key types**:

- **HalideContext**: Manages Halide runtime target selection (host, vulkan, metal, cuda). Provides buffer conversion between Halide `Buffer<>` and cpipe `BufferDescriptor`.
- **VulkanContext** / **MetalContext**: Thin wrappers for native compute shader dispatch. Used only for performance-critical nodes where profiling shows >20% improvement over Halide.
- **InferenceBackend**: Abstract interface for AI model execution. Two implementations:
  - **ExecuTorchBackend**: Loads `.pte` models, manages delegates (XNNPACK, QNN, Vulkan).
  - **OnnxRuntimeBackend**: Loads `.onnx` models, manages execution providers (CUDA, CoreML, NNAPI).
- **InferenceSession**: A loaded model ready for inference. Manages input/output tensor allocation and execution.

### Plugin System

**Responsibility**: Load, register, and manage node plugins at runtime.

**Design**: All nodes (including built-in ISP algorithms) are compiled as shared libraries exposing a C ABI. This ensures a uniform interface and allows third-party extensions without recompiling cpipe.

**C ABI Interface** (`include/cpipe/node_plugin.h`):

```c
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

**Plugin discovery**: Scan configured plugin directories, load `.so`/`.dylib`/`.dll` files, call `cpipe_plugin_init()`, register nodes in the `PluginRegistry`.

**Versioning**: `cpipe_node_info_t` includes `abi_version` (checked at load time) and `node_version` (semantic versioning for the plugin itself).

### Pipeline Engine

**Responsibility**: Load pipeline definitions, build execution DAGs, schedule across devices, and collect profiling data.

**Key types**:

- **Pipeline**: Parsed and validated pipeline definition. Contains node instances, edges, and parameter bindings. Immutable after loading.
- **PipelineLoader**: Reads pipeline JSON, validates against JSON Schema, resolves plugin references from `PluginRegistry`, instantiates nodes.
- **DagScheduler**: Wraps Taskflow `tf::Taskflow`. Converts pipeline edges into Taskflow task dependencies. Executes the DAG with the Taskflow executor.
- **DeviceAllocator**: Assigns each node to a device (CPU, GPU, NPU) based on:
  1. Node's `supported_devices` from `cpipe_node_info_t`
  2. Pipeline JSON device hints
  3. Current device availability and load
  4. Buffer location (prefer keeping data on-device)
- **Profiler**: Records per-node execution time, memory high-water mark, device utilization. Data exposed via JSON-RPC for the pipeline editor.

### ISP Node Plugins

**Responsibility**: Implement individual image processing algorithms.

Each node is a separate shared library in the `plugins/` directory. Built-in nodes ship with cpipe but use the exact same interface as third-party plugins.

See [isp.md](isp.md) for detailed specifications of each node.

### Application Layer

**CLI** (`cpipe`):

| Subcommand | Description |
|------------|-------------|
| `process` | Process a DNG/RAW file through a pipeline, output HEIF |
| `list-plugins` | Show available plugins and their versions |
| `inspect` | Validate and display a pipeline JSON file |
| `benchmark` | Run a pipeline with profiling enabled, output timing report |
| `serve` | Start WebSocket server for pipeline editor connection |

**Android App** (M5):
- Kotlin/Java thin layer for Camera2 API and Android UI
- JNI bridge to native cpipe library
- Preview pipeline (downscaled, low-latency) and capture pipeline (full-resolution)

**Pipeline Editor** (M3):
- React Flow 12.x single-page application
- Deployed as static site on GitHub Pages
- Connects to `cpipe serve` via WebSocket (JSON-RPC)
- All data stored on cpipe side; editor is a pure view layer

## Key Interfaces (Pseudocode)

### BufferDescriptor

```
BufferDescriptor:
    width:     uint32
    height:    uint32
    format:    PixelFormat  (BAYER_RGGB_16, BAYER_BGGR_16, ...,
                             RGB_16, RGB_8, RGBA_8, FLOAT32, ...)
    stride:    uint32       (bytes per row, includes padding)
    device:    DeviceType   (CPU, GPU, NPU)
    data:      void*        (opaque pointer to pixel data)
    size:      uint64       (total bytes)
```

### NodeInfo

```
NodeInfo:
    plugin_id:         string    (e.g., "cpipe.isp.demosaic")
    display_name:      string    (e.g., "Demosaic (Malvar)")
    version:           string    (semver, e.g., "1.0.0")
    abi_version:       uint32    (must match host ABI version)
    input_count:       uint32
    output_count:      uint32
    supported_devices: DeviceFlags  (CPU | GPU | NPU bitmask)
    category:          string    (e.g., "isp.preprocessing", "isp.core", "ai.enhancement")
```

### InferenceBackend

```
InferenceBackend:
    load_model(path: string, options: InferenceOptions) -> InferenceSession
    name() -> string          (e.g., "executorch", "onnxruntime")
    available() -> bool       (runtime check for backend availability)

InferenceSession:
    run(inputs: Tensor[], outputs: Tensor[]) -> Status
    get_input_info()  -> TensorInfo[]
    get_output_info() -> TensorInfo[]

TensorInfo:
    name:   string
    shape:  int[]
    dtype:  DataType  (FLOAT32, FLOAT16, INT8, UINT8)
```

## Pipeline JSON Format

```json
{
  "version": "1.0",
  "name": "Default RAW to sRGB",
  "description": "Basic single-frame ISP pipeline",
  "nodes": [
    {
      "id": "blc",
      "plugin": "cpipe.isp.blc",
      "params": {
        "use_dng_metadata": true
      },
      "device_hint": "cpu"
    },
    {
      "id": "bad_pixel",
      "plugin": "cpipe.isp.bad_pixel",
      "params": {
        "detection_threshold": 50,
        "window_size": 5
      }
    },
    {
      "id": "lsc",
      "plugin": "cpipe.isp.lsc",
      "params": {
        "gain_map_source": "dng"
      }
    },
    {
      "id": "demosaic",
      "plugin": "cpipe.isp.demosaic",
      "params": {
        "algorithm": "malvar"
      },
      "device_hint": "gpu"
    },
    {
      "id": "awb",
      "plugin": "cpipe.isp.awb",
      "params": {
        "algorithm": "gray_world"
      }
    },
    {
      "id": "ccm",
      "plugin": "cpipe.isp.ccm",
      "params": {
        "matrix_source": "dng",
        "target_colorspace": "srgb"
      }
    },
    {
      "id": "gamma",
      "plugin": "cpipe.isp.gamma",
      "params": {
        "curve": "srgb",
        "output_bits": 8
      }
    }
  ],
  "edges": [
    { "from": ["blc", "output"], "to": ["bad_pixel", "input"] },
    { "from": ["bad_pixel", "output"], "to": ["lsc", "input"] },
    { "from": ["lsc", "output"], "to": ["demosaic", "input"] },
    { "from": ["demosaic", "output"], "to": ["awb", "input"] },
    { "from": ["awb", "output"], "to": ["ccm", "input"] },
    { "from": ["ccm", "output"], "to": ["gamma", "input"] }
  ],
  "metadata": {
    "author": "cpipe",
    "required_plugins": [
      "cpipe.isp.blc@^1.0",
      "cpipe.isp.bad_pixel@^1.0",
      "cpipe.isp.lsc@^1.0",
      "cpipe.isp.demosaic@^1.0",
      "cpipe.isp.awb@^1.0",
      "cpipe.isp.ccm@^1.0",
      "cpipe.isp.gamma@^1.0"
    ]
  }
}
```

## WebSocket JSON-RPC Protocol

Communication between the pipeline editor (React Flow web app) and `cpipe serve`.

### Methods

| Method | Direction | Description |
|--------|-----------|-------------|
| `pipeline.load` | Editor → cpipe | Load a pipeline JSON into memory |
| `pipeline.save` | Editor → cpipe | Save current pipeline to JSON file |
| `pipeline.run` | Editor → cpipe | Execute the loaded pipeline on an input image |
| `pipeline.get` | Editor → cpipe | Get the current pipeline definition |
| `node.set_param` | Editor → cpipe | Update a node's parameter value |
| `node.get_param` | Editor → cpipe | Get a node's current parameters |
| `node.get_schema` | Editor → cpipe | Get a node's parameter JSON Schema |
| `preview.start` | Editor → cpipe | Begin streaming preview frames |
| `preview.stop` | Editor → cpipe | Stop preview streaming |
| `plugin.list` | Editor → cpipe | List all available plugins |

### Notifications (cpipe → Editor)

| Notification | Description |
|-------------|-------------|
| `preview.frame` | New preview image available (binary WebSocket frame follows) |
| `pipeline.progress` | Execution progress update |
| `pipeline.profile` | Per-node profiling data after execution |
| `pipeline.error` | Error during execution |

### Message Format

```json
{
  "jsonrpc": "2.0",
  "method": "node.set_param",
  "id": 42,
  "params": {
    "node_id": "awb",
    "key": "algorithm",
    "value": "gray_world"
  }
}
```

## Target Directory Structure

```
cpipe/
├── CMakeLists.txt                  # Root: project(), options, add_subdirectory() only
├── CMakePresets.json               # Build presets (linux, macos, windows, android)
├── vcpkg.json                      # vcpkg manifest (dependencies)
├── .clang-format                   # C++ formatting rules
├── .editorconfig                   # Editor settings
├── .gitignore
├── LICENSE                         # Apache 2.0
├── README.md
├── CLAUDE.md                       # Agent development guide
├── CHANGELOG.md
│
├── docs/
│   ├── architecture.md             # This document
│   ├── tech.md                     # Technology selections
│   ├── isp.md                      # ISP node reference + SOTA survey
│   └── roadmap.md                  # Milestone roadmap
│
├── include/                        # Public headers only (minimal API surface)
│   └── cpipe/
│       ├── node_plugin.h           # C ABI plugin interface (C-only header)
│       ├── buffer.h                # BufferDescriptor, BufferPool public API
│       ├── types.h                 # Common types (PixelFormat, DeviceType, Status)
│       └── version.h               # Version macros
│
├── src/
│   ├── CMakeLists.txt              # Orchestrates src/ subtargets
│   │
│   ├── common/                     # Cross-layer shared utilities
│   │   ├── CMakeLists.txt          # target: cpipe_common (STATIC)
│   │   ├── error.h / error.cpp     #   Error types, cpipe_status_t helpers
│   │   ├── log.h / log.cpp         #   spdlog initialization, global logger
│   │   └── json_utils.h / .cpp     #   JSON parsing/serialization helpers
│   │
│   ├── platform/                   # Platform abstraction layer
│   │   ├── CMakeLists.txt          # target: cpipe_platform (STATIC)
│   │   ├── common/                 #   Abstract interfaces + platform-agnostic code
│   │   │   ├── buffer_pool.h / .cpp
│   │   │   ├── buffer_descriptor.h
│   │   │   ├── dng_reader.h        #   Abstract DNG reader interface
│   │   │   └── heif_writer.h       #   Abstract HEIF writer interface
│   │   ├── linux/                  #   Linux/desktop implementations
│   │   │   ├── vulkan_buffer_backend.h / .cpp
│   │   │   ├── libraw_dng_reader.h / .cpp
│   │   │   └── libheif_writer.h / .cpp
│   │   ├── android/                #   Android implementations
│   │   │   ├── ahardware_buffer_backend.h / .cpp
│   │   │   ├── platform_dng_reader.h / .cpp
│   │   │   └── mediacodec_heif_writer.h / .cpp
│   │   └── apple/                  #   macOS/iOS implementations
│   │       ├── metal_buffer_backend.h / .cpp
│   │       └── ...
│   │
│   ├── compute/                    # Compute layer
│   │   ├── CMakeLists.txt          # target: cpipe_compute (STATIC)
│   │   ├── halide/                 #   Halide runtime context + buffer bridge
│   │   │   ├── halide_context.h / .cpp
│   │   │   └── halide_buffer_bridge.h / .cpp
│   │   ├── vulkan/                 #   Native Vulkan compute (optimization only)
│   │   │   └── vulkan_context.h / .cpp
│   │   ├── metal/                  #   Native Metal compute (optimization only)
│   │   │   └── metal_context.h / .mm
│   │   └── inference/              #   AI inference abstraction
│   │       ├── inference_backend.h #   Abstract interface
│   │       ├── inference_session.h
│   │       ├── executorch/         #   ExecuTorch backend
│   │       │   ├── CMakeLists.txt  #   Guarded by option(WITH_EXECUTORCH)
│   │       │   └── executorch_backend.h / .cpp
│   │       └── onnxruntime/        #   ONNX Runtime backend
│   │           ├── CMakeLists.txt  #   Guarded by option(WITH_ONNXRUNTIME)
│   │           └── onnx_backend.h / .cpp
│   │
│   ├── engine/                     # Pipeline engine
│   │   ├── CMakeLists.txt          # target: cpipe_engine (STATIC)
│   │   ├── loader/                 #   JSON pipeline loader, schema validation
│   │   │   ├── pipeline_loader.h / .cpp
│   │   │   └── schema_validator.h / .cpp
│   │   ├── scheduler/              #   DAG scheduling (Taskflow wrapper)
│   │   │   ├── dag_scheduler.h / .cpp
│   │   │   └── device_allocator.h / .cpp
│   │   └── profiler/               #   Per-node timing, memory tracking
│   │       └── profiler.h / .cpp
│   │
│   ├── plugin/                     # Plugin system
│   │   ├── CMakeLists.txt          # target: cpipe_plugin (STATIC)
│   │   ├── plugin_loader.h / .cpp  #   dlopen/LoadLibrary wrapper
│   │   └── plugin_registry.h / .cpp
│   │
│   └── cli/                        # CLI application
│       ├── CMakeLists.txt          # target: cpipe (EXECUTABLE)
│       ├── main.cpp                #   CLI11 subcommand routing
│       ├── cmd_process.cpp         #   `cpipe process` subcommand
│       ├── cmd_list_plugins.cpp
│       ├── cmd_inspect.cpp
│       ├── cmd_benchmark.cpp
│       └── cmd_serve.cpp           #   WebSocket server for editor
│
├── halide/                         # Halide AOT generators (host-side executables)
│   ├── CMakeLists.txt              # Builds generators, calls add_halide_library()
│   ├── blc_generator.cpp           # Generates optimized BLC kernel for target platform
│   ├── lsc_generator.cpp
│   ├── bad_pixel_generator.cpp
│   ├── demosaic_generator.cpp
│   ├── awb_generator.cpp
│   ├── ccm_generator.cpp
│   └── gamma_generator.cpp
│
├── plugins/                        # Node plugins (each is a shared library)
│   ├── CMakeLists.txt              # Iterates type subdirectories
│   ├── isp/                        # Classical ISP nodes (Halide-based)
│   │   ├── CMakeLists.txt
│   │   ├── blc/                    #   Each plugin is self-contained:
│   │   │   ├── CMakeLists.txt      #     target: cpipe_isp_blc (MODULE)
│   │   │   ├── blc.h              #     Internal header
│   │   │   ├── blc.cpp            #     Implementation + C ABI exports
│   │   │   └── blc_test.cpp       #     Plugin-specific unit test
│   │   ├── lsc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── lsc.h / lsc.cpp
│   │   │   └── lsc_test.cpp
│   │   ├── bad_pixel/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── bad_pixel.h / .cpp
│   │   │   └── bad_pixel_test.cpp
│   │   ├── demosaic/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── demosaic.h / .cpp
│   │   │   └── demosaic_test.cpp
│   │   ├── awb/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── awb.h / .cpp
│   │   │   └── awb_test.cpp
│   │   ├── ccm/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ccm.h / .cpp
│   │   │   └── ccm_test.cpp
│   │   └── gamma/
│   │       ├── CMakeLists.txt
│   │       ├── gamma.h / .cpp
│   │       └── gamma_test.cpp
│   ├── ai/                         # AI model nodes (M4)
│   │   ├── CMakeLists.txt
│   │   ├── denoise/                #   NAFNet-based RAW denoising
│   │   │   ├── CMakeLists.txt
│   │   │   ├── denoise.cpp
│   │   │   └── denoise_test.cpp
│   │   ├── awb/                    #   Learned AWB
│   │   │   └── ...
│   │   └── nilut/                  #   Neural 3D LUT color mapping
│   │       └── ...
│   └── io/                         # Utility/IO nodes (future)
│       └── CMakeLists.txt
│
├── tests/
│   ├── CMakeLists.txt              # Registers CTest suites
│   ├── unit/                       # Unit tests (mirrors src/ structure)
│   │   ├── CMakeLists.txt
│   │   ├── common/                 #   Tests for src/common/
│   │   ├── platform/               #   Tests for src/platform/
│   │   ├── compute/                #   Tests for src/compute/
│   │   ├── engine/                 #   Tests for src/engine/
│   │   └── plugin/                 #   Tests for src/plugin/
│   ├── integration/                # Full pipeline integration tests
│   │   └── CMakeLists.txt
│   ├── benchmark/                  # GoogleBenchmark performance tests
│   │   └── CMakeLists.txt
│   └── fixtures/                   # Test data (reference images, pipelines, expected output)
│       ├── images/                 #   Reference DNG files (Git LFS)
│       ├── pipelines/              #   Test pipeline JSON files
│       └── reference/              #   Expected output images for IQA comparison
│
├── examples/
│   └── pipelines/                  # Sample pipeline JSON files
│       ├── default_srgb.json       #   Standard RAW → sRGB pipeline
│       └── minimal.json            #   Minimal: BLC → Demosaic → Gamma
│
├── schemas/
│   └── pipeline.schema.json        # JSON Schema for pipeline format
│
├── tools/
│   └── iqa/                        # Python IQA evaluation scripts
│       ├── evaluate.py
│       └── requirements.txt
│
├── editor/                         # React Flow Pipeline Editor (M3, placeholder)
│
└── android/                        # Android App (M5, placeholder)
```

### Directory Structure Design Notes

**`halide/` (top-level)**: Halide AOT generators are host-side executables with different
build dependencies (link to `Halide::Generator`). Isolating them allows CMake to build
generators for the host platform first, then invoke them to produce optimized kernels for
the target platform. This is the pattern recommended by Halide's official CMake integration.

**`plugins/` categorized by type**: Plugins are grouped into `isp/` (classical Halide-based),
`ai/` (neural network models), and `io/` (utility/IO nodes). This enables selective
installation (e.g., mobile builds may exclude heavy AI plugins) and clear organization
as the plugin count grows.

**Plugin self-containment**: Each plugin directory includes its own `CMakeLists.txt`,
source files, internal headers, and unit tests. This makes plugins independently buildable
and provides a clear template for third-party plugin developers.

**`src/platform/` organized by platform**: Abstract interfaces live in `common/`. Each
platform (`linux/`, `android/`, `apple/`) contains all implementations for that platform
(buffer backend, DNG reader, HEIF writer). CMake selects the correct platform directory
based on the target. This makes it easy to see all code needed for a given platform.

**`src/common/`**: Cross-layer utilities (error types, logging, JSON helpers) that are
used by platform, compute, engine, and plugin layers. Compiled as a static library
(`cpipe_common`) to avoid circular dependencies between layers.

**`src/compute/inference/` split by backend**: The `InferenceBackend` abstract interface
lives in the parent directory. Each backend (`executorch/`, `onnxruntime/`) has its own
`CMakeLists.txt` guarded by CMake options (`WITH_EXECUTORCH`, `WITH_ONNXRUNTIME`),
allowing builds to include only the needed backends.

**`tests/` mirrors `src/`**: Unit tests in `tests/unit/` follow the same directory
structure as `src/` for easy navigation. Plugin-specific tests are colocated within each
plugin directory. `tests/fixtures/` stores shared test data (reference images, pipeline
JSON, expected outputs).

**`examples/pipelines/`**: Sample pipeline JSON files serve as both user documentation
and integration test inputs.
