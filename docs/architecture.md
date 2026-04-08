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
├── CMakeLists.txt              # Root CMake configuration
├── CMakePresets.json            # Build presets (Linux, macOS, Windows, Android)
├── vcpkg.json                  # vcpkg manifest (dependencies)
├── .clang-format               # C++ formatting rules
├── .editorconfig               # Editor settings
├── .gitignore
├── LICENSE                     # Apache 2.0
├── README.md
├── CLAUDE.md                   # Agent development guide
├── CHANGELOG.md
│
├── docs/
│   ├── architecture.md         # This document
│   ├── tech.md                 # Technology selections
│   ├── isp.md                  # ISP node reference + SOTA survey
│   └── roadmap.md              # Milestone roadmap
│
├── include/
│   └── cpipe/
│       ├── node_plugin.h       # C ABI plugin interface (C-only header)
│       ├── buffer.h            # BufferDescriptor, BufferPool API
│       ├── types.h             # Common types (PixelFormat, DeviceType, Status)
│       └── version.h           # Version macros
│
├── src/
│   ├── platform/               # Platform Layer
│   │   ├── buffer/             #   BufferPool, platform-specific backends
│   │   ├── dng/                #   DNG reader (libraw wrapper, Android bridge)
│   │   └── heif/               #   HEIF writer (libheif wrapper, MediaCodec bridge)
│   │
│   ├── compute/                # Compute Layer
│   │   ├── halide/             #   Halide runtime context, buffer bridge
│   │   ├── vulkan/             #   Native Vulkan compute (optimization only)
│   │   ├── metal/              #   Native Metal compute (optimization only)
│   │   └── inference/          #   InferenceBackend, ExecuTorch, ONNX Runtime
│   │
│   ├── engine/                 # Pipeline Engine
│   │   ├── loader/             #   JSON pipeline loader, schema validation
│   │   ├── scheduler/          #   DagScheduler (Taskflow), DeviceAllocator
│   │   └── profiler/           #   Per-node timing, memory tracking
│   │
│   ├── plugin/                 # Plugin System
│   │   └── loader/             #   Dynamic library loader, PluginRegistry
│   │
│   └── cli/                    # CLI Application
│       └── main.cpp            #   CLI11 subcommand routing
│
├── plugins/                    # Built-in ISP Node Plugins
│   ├── isp_blc/                #   Black Level Correction
│   ├── isp_lsc/                #   Lens Shading Correction
│   ├── isp_bad_pixel/          #   Bad Pixel Correction
│   ├── isp_demosaic/           #   CFA Demosaicing
│   ├── isp_awb/                #   Auto White Balance
│   ├── isp_ccm/                #   Color Correction Matrix
│   └── isp_gamma/              #   Gamma / Tone Curve
│
├── tests/
│   ├── unit/                   # GoogleTest unit tests
│   ├── integration/            # Full pipeline integration tests
│   └── benchmark/              # GoogleBenchmark performance tests
│
├── tools/
│   └── iqa/                    # Python IQA evaluation scripts
│       ├── evaluate.py
│       └── requirements.txt    # IQA-PyTorch + dependencies
│
├── schemas/
│   └── pipeline.schema.json    # JSON Schema for pipeline format
│
├── editor/                     # React Flow Pipeline Editor (M3)
│   ├── package.json
│   ├── src/
│   └── public/
│
└── android/                    # Android App (M5)
    ├── app/
    └── build.gradle.kts
```
