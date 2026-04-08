# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Project documentation: README, architecture, technology selections, ISP reference, roadmap
- CLAUDE.md agent development guide with code conventions and document update rules
- Apache 2.0 license
- clang-format configuration for C++20
- editorconfig for consistent formatting
- gitignore for C++/CMake/IDE/vcpkg patterns

### Changed

- Refined directory structure in architecture.md: added `halide/` top-level for AOT generator isolation, categorized plugins into `isp/`, `ai/`, `io/`, organized `src/platform/` by platform (`common/`, `linux/`, `android/`, `apple/`), added `src/common/` for shared utilities, split `src/compute/inference/` by backend, added `tests/fixtures/` and `examples/pipelines/`
- Updated CLAUDE.md directory map to match refined structure with all new paths and plugin colocation notes
