# cpipe

Computational Photography Pipeline -- a professional camera app core with a customizable, DAG-based soft ISP that processes RAW images.

## Features

- **DAG-based Pipeline**: Compose ISP processing nodes into arbitrary directed acyclic graphs
- **Plugin Architecture**: All nodes are plugins (shared libraries via C ABI), including built-in ISP algorithms
- **Dual Compute**: Classical algorithms (Halide v21.0) alongside AI models (ExecuTorch + ONNX Runtime)
- **Cross-platform**: Linux, macOS, Windows, Android, iOS
- **Zero-copy Buffers**: Unified BufferPool with AHardwareBuffer (Android) and Vulkan buffer backends
- **Visual Editor**: React Flow-based pipeline editor with live parameter tuning via WebSocket
- **CLI**: Process RAW images, benchmark pipelines, inspect configurations, serve the web editor
- **HEIF Output**: Modern format with 10-bit HDR support (no JPEG)

## Quick Start

### Prerequisites

- C++20 compiler (GCC 13+, Clang 15+, MSVC 2022)
- CMake 3.25+
- vcpkg

### Build

```bash
git clone https://github.com/user/cpipe.git
cd cpipe
cmake --preset default
cmake --build --preset default
ctest --preset default
```

### Usage

```bash
# Process a RAW/DNG file through a pipeline
cpipe process input.dng -p pipeline.json -o output.heif

# List available plugins
cpipe list-plugins

# Validate and inspect a pipeline file
cpipe inspect pipeline.json

# Benchmark a pipeline (profiling enabled)
cpipe benchmark input.dng -p pipeline.json

# Start WebSocket server for the pipeline editor
cpipe serve --port 8080
```

## Architecture

cpipe is organized into five layers: **Platform** (buffer management, I/O), **Compute** (Halide, Vulkan/Metal, AI inference), **Plugin System** (C ABI dynamic loading), **Pipeline Engine** (DAG scheduling via Taskflow), and **Application** (CLI, Android app, web editor). All ISP nodes are plugins that can be swapped, configured, and shared as JSON pipeline definitions.

See [docs/architecture.md](docs/architecture.md) for the full system design.

## Documentation

- [Architecture](docs/architecture.md) -- System design, layer diagrams, key interfaces
- [Technology Selections](docs/tech.md) -- Dependency rationale, versions, and alternatives
- [ISP Reference](docs/isp.md) -- SOTA algorithms, node specifications, IQA metrics
- [Roadmap](docs/roadmap.md) -- Milestone-based development plan (M0-M7)

## Project Status

Currently in **M0** (scaffolding and documentation). See the [roadmap](docs/roadmap.md) for details.

## License

Apache 2.0 -- see [LICENSE](LICENSE).
