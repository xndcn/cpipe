---
title: "C++20 Project Scaffolding with CMake Presets, vcpkg, and GitHub Actions CI"
date: "2026-04-10"
category: best-practices
module: cpipe-project-scaffolding
problem_type: best_practice
component: development_workflow
severity: high
applies_when:
  - "Setting up a new C++20 project with CMake presets and vcpkg"
  - "Configuring GoogleTest with CTest discovery in a multi-directory layout"
  - "Building a cross-platform CI pipeline for C++ with GitHub Actions"
  - "Establishing a minimal scaffold that proves the entire build chain end-to-end"
tags:
  - cmake
  - vcpkg
  - cpp20
  - googletest
  - github-actions
  - project-scaffolding
  - ci-cd
related_components:
  - testing_framework
  - tooling
---

# C++20 Project Scaffolding with CMake Presets, vcpkg, and GitHub Actions CI

## Context

Starting a C++20 project requires dozens of interlocking decisions: CMake version and preset layout, package manager integration, CI matrix, test framework wiring, and which minimal source stubs prove the toolchain works. Getting these wrong compounds friction -- a broken build system blocks all downstream development. The M0 pattern ("prove the build chain before writing domain code") establishes a minimal scaffold where `cmake --preset default && cmake --build --preset default && ctest --preset default` succeeds on both local machines and CI before any real logic exists.

## Guidance

### 1. Three-File Foundation: CMakePresets.json + vcpkg.json + Root CMakeLists.txt

**CMakePresets.json** -- Use a hidden `base` preset for shared config, derive `default` (Debug) and `release`:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      }
    },
    {
      "name": "default",
      "inherits": "base",
      "binaryDir": "${sourceDir}/build/default",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "inherits": "base",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ]
}
```

**vcpkg.json** -- Declare only what the current milestone needs:

```json
{
  "name": "cpipe",
  "version-string": "0.0.1",
  "dependencies": ["spdlog", "gtest"]
}
```

**Root CMakeLists.txt** -- C++20 strict, gated tests, compile commands exported:

```cmake
cmake_minimum_required(VERSION 3.25)
project(cpipe VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Key decisions: Reference vcpkg via `$env{VCPKG_ROOT}` (no git submodule). Use `CMAKE_CXX_EXTENSIONS OFF` for portable C++20.

### 2. Version Injection via configure_file

Use CMake `configure_file` to inject `PROJECT_VERSION` into source, ensuring the version string is always in sync with CMakeLists.txt:

```cmake
# src/common/CMakeLists.txt
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    @ONLY
)
add_library(cpipe_core STATIC ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)
target_include_directories(cpipe_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
add_library(cpipe::core ALIAS cpipe_core)
```

```cpp
// version.cpp.in
#include <cpipe/version.h>
namespace cpipe {
std::string_view version_string() noexcept { return "@PROJECT_VERSION@"; }
}
```

The `.in` file lives in `src/common/`; CMake generates the actual `.cpp` in the build directory. The alias `cpipe::core` follows CMake namespaced target conventions.

### 3. Test Directory Mirrors Source Directory

```
src/common/version.cpp.in  -->  tests/unit/common/version_test.cpp
```

Each directory level gets a CMakeLists.txt only when it has content. Use `gtest_discover_tests()` (not `add_test()`) so CTest discovers individual TEST cases at build time:

```cmake
add_executable(version_test version_test.cpp)
target_link_libraries(version_test PRIVATE cpipe::core GTest::gtest_main)
gtest_discover_tests(version_test)
```

### 4. Tests That Validate the Build Chain

M0 tests serve a dual purpose -- they validate the toolchain, not just code:

```cpp
TEST(Version, ReturnsNonEmpty) {
    EXPECT_FALSE(cpipe::version_string().empty());
}
TEST(Version, MatchesSemverFormat) {
    std::regex semver(R"(\d+\.\d+\.\d+)");
    EXPECT_TRUE(std::regex_match(std::string(cpipe::version_string()), semver));
}
TEST(Version, MatchesExpected) {
    EXPECT_EQ(cpipe::version_string(), "0.0.1");
}
```

If these pass: CMake configuration works, `configure_file` substitution works, the library compiles and links, GoogleTest is found, and CTest discovers tests.

### 5. CI: Lukka Actions for CMake + vcpkg

Use `lukka/run-vcpkg@v11` and `lukka/run-cmake@v10` to avoid reimplementing vcpkg bootstrap and preset invocation in CI:

```yaml
steps:
  - uses: actions/checkout@v4
  - uses: lukka/get-cmake@latest
  - uses: lukka/run-vcpkg@v11
  - uses: lukka/run-cmake@v10
    with:
      configurePreset: default
      buildPreset: default
      testPreset: default
```

Run `clang-format --dry-run --Werror` as a separate job for fast style feedback without blocking the build.

### 6. Multi-Level CMakeLists.txt Only Where Content Exists

Create CMakeLists.txt files only in directories with actual sources. Do not create placeholders in empty directories -- add them when content arrives. This keeps the build tree honest.

## Why This Matters

- **Single-command builds**: `cmake --preset default` works identically everywhere. No manual `-D` flags, no drifting wiki instructions.
- **Version truth from one source**: `configure_file` eliminates version.h / CMakeLists.txt drift.
- **CI catches breakage early**: Format check + build matrix catches style regressions and platform-specific failures before main.
- **Minimal M0 reduces risk**: Proving the entire toolchain (CMake, vcpkg, Ninja, C++20, GoogleTest, CTest, CI) with one function and three tests, before introducing domain complexity.

## When to Apply

- Starting any new C++20 project with CMake + vcpkg
- Projects that must build on Linux and macOS (or more platforms)
- Establishing CI before significant domain code exists
- Projects using FetchContent dependencies alongside vcpkg-managed deps

## Examples

### Before: Ad-hoc CMake

```bash
# Developer A
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/home/alice/vcpkg/scripts/buildsystems/vcpkg.cmake

# Developer B on macOS -- different generator, path, build type
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/Users/bob/tools/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### After: Preset-Driven

```bash
# Every developer, every platform, same commands
cmake --preset default
cmake --build --preset default
ctest --preset default
```

### Before: Manual Version Strings

```cpp
constexpr auto VERSION = "0.0.1";  // Drifts from CMakeLists.txt project(VERSION ...)
```

### After: configure_file Injection

```cmake
project(cpipe VERSION 0.0.2 LANGUAGES CXX)
# version.cpp.in: return "@PROJECT_VERSION@";
# Generated version.cpp: return "0.0.2";  -- always in sync
```

## Related

- [docs/tech.md](../../tech.md) -- Technology selections and vcpkg/CMake rationale
- [docs/architecture.md](../../architecture.md) -- Target directory structure
- [docs/roadmap.md](../../roadmap.md) -- M0 milestone definition
