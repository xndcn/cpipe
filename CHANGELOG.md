# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `include/cpipe/types.h`: C-compatible public type definitions — `cpipe_status_t`, `cpipe_pixel_format_t`, `cpipe_device_type_t`, `cpipe_buffer_t`, `cpipe_node_info_t`
- `include/cpipe/error.h`: public `cpipe::Error` struct and `cpipe::expected<T,E>` alias (tl::expected polyfill with automatic std::expected promotion when available)
- `include/cpipe/buffer.h`: public `BufferDescriptor`, `Buffer`, and `BufferPool` C++ API
- `src/common/types.hpp`: C++ enum wrappers (`PixelFormat`, `DeviceType`) with `bytes_per_pixel()` helper
- `src/common/error.h/cpp`: `status_to_string()` and `make_error()` utilities
- `src/common/log.h/cpp`: spdlog-backed `cpipe::log` namespace with `init()`/`get()` and `CPIPE_LOG_*` macros; level overridable via `CPIPE_LOG_LEVEL` env var
- `src/common/json_utils.h/cpp`: `cpipe::json::parse_string()`, `parse_file()`, and `get<T>()` returning `expected<T, Error>`
- `src/platform/common/buffer_pool.cpp`: `BufferPool` implementation with 64-byte-aligned allocation via `std::aligned_alloc`, free-list keyed by full `BufferDescriptor` (device-aware reuse), and thread-safe ref-counted buffer lifecycle
- `cpipe_platform` CMake target linking `cpipe::common`
- `nlohmann-json` and `tl-expected` vcpkg dependencies
- 51 new unit tests across `error_test`, `log_test`, `json_utils_test`, `buffer_pool_test` (54 total, up from 3)

### Changed

- Renamed `cpipe_core` → `cpipe_common` (library target and alias `cpipe::common`); `cpipe::core` alias kept for compatibility
- `cpipe_device_type_t` values changed to power-of-two (CPU=1, GPU=2, NPU=4) so `supported_devices` bitmask works correctly
- `BufferPool` free-list keyed by full `BufferDescriptor` instead of byte size alone, preventing cross-device buffer reuse
- `BufferDescriptor::create()` rejects unknown pixel formats (bytes_per_pixel == 0 → `CPIPE_STATUS_ERROR_INVALID_PARAM`)
- `BufferDescriptor::create()` rejects dimensions that would overflow stride or total size calculations
- `BufferPool::allocate()` guards against `size_t` truncation on 32-bit targets (SIZE_MAX check)
- `BufferDescriptor` default constructor made private — only `create()` factory can construct valid descriptors
- `Buffer` default constructor removed; only `BufferPool::allocate()` can create valid handles
- Logger lazy init (`cpipe::log::get()`) serialized via `std::call_once` to prevent race on concurrent first use
- CMake build system with C++20 support, `cpipe_core` static library, and `cpipe::core` alias target
- `CMakePresets.json` with `default` (Debug) and `release` presets using Ninja generator and vcpkg toolchain
- `vcpkg.json` manifest with `spdlog` and `gtest` dependencies
- `include/cpipe/version.h` public header declaring `cpipe::version_string()`
- `src/common/version.cpp.in` CMake-configured source baking project version at build time
- GoogleTest harness: `tests/unit/common/version_test.cpp` with 3 tests covering `version_string()`
- GitHub Actions CI workflow: `format-check` job (clang-format) + `build` matrix (ubuntu-24.04/GCC, macos-14/Apple Clang) with artifact upload
- Project documentation: README, architecture, technology selections, ISP reference, roadmap
- CLAUDE.md agent development guide with code conventions and document update rules
- Apache 2.0 license
- clang-format configuration for C++20
- editorconfig for consistent formatting
- gitignore for C++/CMake/IDE/vcpkg patterns

### Changed

- Refined directory structure in architecture.md: added `halide/` top-level for AOT generator isolation, categorized plugins into `isp/`, `ai/`, `io/`, organized `src/platform/` by platform (`common/`, `linux/`, `android/`, `apple/`), added `src/common/` for shared utilities, split `src/compute/inference/` by backend, added `tests/fixtures/` and `examples/pipelines/`
- Updated CLAUDE.md directory map to match refined structure with all new paths and plugin colocation notes
