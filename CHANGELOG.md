# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
