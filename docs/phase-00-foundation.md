# Phase 0 — Foundation

> Date: 2026-05-11 · Phase tag: `v0.1` · Parent: [`roadmap.md`](roadmap.md) · References: [`architecture.md`](architecture.md), [`tech.md`](tech.md), [`buffer.md`](buffer.md), [`plugin-sdk.md`](plugin-sdk.md)

This document is the detailed plan for Phase 0 of the cpipe v1.0 roadmap. P0's purpose is to stand up a public GitHub repository, a CMake build, a green CI matrix, the core data types, the plugin ABI skeleton, and one passthrough node — a repository skeleton that **compiles, tests, and runs end-to-end** on Linux.

P0 is the foundation everything else stands on. The goal is *not* a working ISP pipeline. The goal is: a developer can clone the repo, run `cmake --preset linux-debug`, run `ctest`, and run `cpipe run` with a one-node passthrough pipeline.

When P0 is done, the project is tagged `v0.1` and Phase 1 begins.

---

## 1. Objective

A repository skeleton that compiles, tests, and runs a trivial passthrough node end-to-end on Linux x86_64.

**Success looks like:**
- Public GitHub repo at `github.com/xndcn/cpipe`.
- Green CI matrix on every push and PR.
- A passthrough node implemented as a Halide AOT pipeline, registered via `CPIPE_REGISTER_NODE`, loaded by the runtime, dispatched by the (single-threaded) scheduler, and invoked from `cpipe run`.
- 8–12 unit tests plus 1 integration smoke test.

P0 explicitly does *not* deliver DNG ingest, HEIF output, color management, or any real ISP node. That is Phase 1.

---

## 2. Inputs

- Locked design documents in `docs/research/`, `docs/architecture.md`, `docs/buffer.md`, `docs/plugin-sdk.md`, `docs/tech.md`, `docs/roadmap.md`.
- A development machine running Ubuntu 24.04+ (or equivalent) with NVIDIA RTX-class GPU and Vulkan 1.3 drivers per [RD-21](roadmap.md#1-decision-quick-reference).
- A GitHub account (`xndcn`) with Actions enabled.

No prior code, no prior tags — P0 starts from an empty repo.

---

## 3. Outputs

- Public GitHub repository at `github.com/xndcn/cpipe`.
- Tag `v0.1` on the green build that satisfies the DoD in §10.
- The repository layout described in §5.
- A green GitHub Actions workflow `build-and-test.yml`.
- A `cpipe` CLI binary on Linux x86_64.

---

## 4. Phase Decisions (PD-N)

P0-specific decisions, locked from the planning Q&A. Where a P0 decision narrows a roadmap-level [RD-NN](roadmap.md#1-decision-quick-reference), that link is cited.

| ID    | Decision                                         | Value                                                                                                                                                                                                                                |
|-------|--------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| PD-1  | Repo location                                    | `github.com/xndcn/cpipe`. Public from day one ([RD-17](roadmap.md#1-decision-quick-reference)).                                                                                                                                       |
| PD-2  | Repo top-level files                             | `LICENSE` (Apache 2.0) + `README.md` only. No `CONTRIBUTING.md` / `CHANGELOG.md` / `NOTICE` / `CODE_OF_CONDUCT.md` / `SECURITY.md` in P0; add when concretely needed.                                                                  |
| PD-3  | Git branch model                                 | `main` only; releases are tags (`v0.1`, `v0.2`, …). Feature work happens on short-lived branches PR'd into `main`.                                                                                                                    |
| PD-4  | C++ namespace convention                         | Top-level `cpipe`, sub-namespaces `cpipe::compute / runtime / sdk / nodes / ingest / color / server` (per [`buffer.md`](buffer.md) / [`plugin-sdk.md`](plugin-sdk.md)).                                                                |
| PD-5  | Node ID format                                   | Reverse-DNS `com.cpipe.<category>.<name>` (e.g. `com.cpipe.builtin.passthrough`), per [`plugin-sdk.md` §3](plugin-sdk.md#3-c-abi-cpipe_nodeh). Existing docs need no change.                                                          |
| PD-6  | CMake minimum                                    | 3.28+ ([`tech.md` §2](tech.md#2-layer-1--build--toolchain)).                                                                                                                                                                          |
| PD-7  | C++ standard                                     | C++20 with `tl::expected` polyfill for `std::expected` ([`plugin-sdk.md` §10.2](plugin-sdk.md#10-error-handling-result-style)).                                                                                                       |
| PD-8  | Compiler options                                 | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` (Debug + Release). RTTI **on** (required for `dynamic_cast<VulkanBuffer*>` in [`buffer.md` §5](buffer.md#5-ibuffer-interface)). Exceptions **on** (`-fexceptions`), but project code does not `throw` — errors go through `Result<T>`. SDK dispatch shim `catch`es plugin-side exceptions ([`plugin-sdk.md` §6.1](plugin-sdk.md#61-the-dispatch-shim)). |
| PD-9  | CMake targets in P0                              | All six targets per [`architecture.md` §3](architecture.md#3-native-module-decomposition): `cpipe-core`, `cpipe-runtime`, `cpipe-sdk`, `cpipe-builtin-nodes`, `cpipe-server`, `cpipe-cli`. Skeletons compile even when empty.         |
| PD-10 | P0 dependencies (8 packages)                     | `Halide` v21 (FetchContent), `TaskFlow` v4.0.0 (vcpkg), `Catch2` v3 (vcpkg), `spdlog` (vcpkg), `nlohmann/json` (vcpkg), `nlohmann/json-schema-validator` (vcpkg), `tl::expected` (vcpkg), `CLI11` (vcpkg). VMA / Vulkan-Headers / Slang / OCIO / lcms2 / libheif / LibRaw / ExecuTorch / Tracy slip to later phases. |
| PD-11 | Halide AOT integration                           | P0 builds **one** Halide generator (`passthrough_copy`) targeting `host` (CPU). The `add_halide_library()` CMake macro is wired in. Vulkan / Hexagon AOT targets reserved for P1+.                                                  |
| PD-12 | CI shape                                         | Single workflow `build-and-test.yml` with four parallel jobs: `lint` (clang-format check + clang-tidy), `build-debug` (ASAN + UBSAN), `build-release`, `test` (ctest after debug build). Matrix: `ubuntu-24.04` only.                |
| PD-13 | CI caching                                       | vcpkg binary cache via `actions/cache` keyed on `vcpkg.json` hash, plus `ccache` keyed on commit + compiler version. Cold first build budget: ≤ 20 min.                                                                              |
| PD-14 | `cpipe-core` scope in P0                         | `PixelFormat`, `BufferLayout`, `BufferKind`, `BufferUsage`, `IBuffer` interface, status codes, **and** a working `CpuBuffer` (`posix_memalign`-backed) implementation. GPU/AHB/IOSurface `IBuffer` subclasses slip to P1+.            |
| PD-15 | Plugin ABI scope in P0                           | All four suites (`buffer / compute / param / inference`) declared in `cpipe_node.h`. `buffer / compute / param` host-side implementations are functional for the Passthrough node. `inference` host-side implementation returns `CPIPE_UNSUPPORTED` for any call. |
| PD-16 | ABI header location                              | `include/cpipe/sdk/cpipe_node.h` — part of the `cpipe-sdk` target.                                                                                                                                                                    |
| PD-17 | Passthrough node implementation                  | Halide AOT generator (`passthrough_copy`) running on CPU target; `Passthrough` plugin class submits it through `ComputeContext::submit_halide`. **No CPU memcpy fallback.** End-to-end Halide path is validated from P0.              |
| PD-18 | CLI scope in P0                                  | `cpipe run <input> -p <pipeline.json> -o <output>` only. `info / serve / bench / iqa / model` subcommands slip to later phases.                                                                                                       |
| PD-19 | Pipeline::load completeness                      | Full implementation: nlohmann/json parse → JSON Schema validation against an embedded `pipeline-v0.1.json` schema → topological sort → minimum memory plan (allocates `CpuBuffer` intermediates).                                    |
| PD-20 | Scheduler scope in P0                            | TaskFlow `Executor` is linked and constructed, but P0 dispatches nodes **serially** in topological order. Real parallel TaskFlow scheduling slips to P1+.                                                                             |
| PD-21 | `.clang-format` style                            | `BasedOnStyle: Google`, `ColumnLimit: 100`, `IndentWidth: 4`, `AccessModifierOffset: -4`, `AllowShortFunctionsOnASingleLine: Empty`.                                                                                                  |
| PD-22 | `.clang-tidy` rule set                           | Enable `readability-*`, `modernize-*`, `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `portability-*`. Disable `fuchsia-*`, `llvm-*`, `google-readability-todo`. Baseline-suppress existing warnings on first integration.    |
| PD-23 | `pre-commit` framework                           | Python `pre-commit` framework. Hooks: `clang-format`, `trailing-whitespace`, `end-of-file-fixer`, `check-yaml`, `check-json`. Documented in README.                                                                                   |
| PD-24 | Docs generator                                   | None in P0. `docs/` is markdown only; GitHub renders. Doxygen / mdBook slip to P6 (Polish).                                                                                                                                          |
| PD-25 | Source-file license header                       | Three-line SPDX style: `// SPDX-License-Identifier: Apache-2.0` + `// Copyright (c) 2026 cpipe contributors` + blank line. Applies to every `.h / .hpp / .cpp` authored.                                                              |
| PD-26 | Test coverage in P0                              | 8–12 unit tests (Catch2) + 1 integration smoke test. No coverage percentage gate; tests target the obvious invariants per §8.                                                                                                          |
| PD-27 | Git LFS                                          | **Not enabled** in P0. Test fixtures are generated programmatically (a 64×64 RGBA8 gradient). LFS bootstraps in P1 when EXR golden fixtures appear.                                                                                  |
| PD-28 | Task slicing                                     | Seven vertical tasks (T1–T7); two checkpoints (after T3 and after T7).                                                                                                                                                                |
| PD-29 | vcpkg package name for JSON Schema validator     | The upstream project remains `nlohmann/json-schema-validator` per PD-10, but the vcpkg manifest uses the actual port name `json-schema-validator`; CMake still consumes `nlohmann_json_schema_validator::validator`.                   |
| PD-30 | T1 `cpipe-sdk` artifact kind                     | T1 follows [`architecture.md` §3](architecture.md#3-native-module-decomposition): `cpipe-sdk` is an `INTERFACE` header-only target, so the skeleton produces four static libraries, one header-only SDK target, and one CLI executable. |
| PD-31 | Halide v21 acquisition                           | `cmake/HalideHelpers.cmake` fetches the official Halide v21.0.0 x86_64 Linux binary release archive by default; source builds are avoided in P0 CI to keep the cold-build budget under PD-13.                                         |
| PD-32 | Node schema validation gate                      | P0 keeps pre-commit scoped to PD-23 hooks; node manifest JSON Schema validation runs in Catch2 with `nlohmann_json_schema_validator::validator`, and the Editor's Ajv gate is deferred to P3.                                         |

---

## 5. Repository Layout (P0 end-state)

```
cpipe/
├── .clang-format
├── .clang-tidy
├── .editorconfig
├── .gitattributes
├── .gitignore
├── .pre-commit-config.yaml
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE                          # Apache 2.0
├── README.md                        # pre-alpha warning + build instructions
├── vcpkg.json                       # manifest mode; baseline pinned
├── vcpkg-configuration.json         # registry overrides if needed
├── .github/
│   └── workflows/
│       └── build-and-test.yml       # PD-12
├── cmake/
│   ├── CompilerOptions.cmake        # PD-8 flags
│   ├── HalideHelpers.cmake          # add_halide_library wrapper
│   └── EmbedJson.cmake              # generates manifest .cpp from .json
├── include/
│   └── cpipe/
│       ├── core/
│       │   ├── PixelFormat.hpp
│       │   ├── BufferLayout.hpp
│       │   ├── BufferUsage.hpp
│       │   ├── IBuffer.hpp
│       │   └── Status.hpp
│       └── sdk/
│           ├── cpipe_node.h         # PD-16
│           ├── sdk.hpp
│           ├── registry.hpp         # CPIPE_REGISTER_NODE
│           └── section.hpp          # linker-section macros (Linux ELF first)
├── src/
│   └── cpipe/
│       ├── core/
│       │   ├── BufferLayout.cpp
│       │   └── CpuBuffer.cpp
│       ├── runtime/
│       │   ├── Pipeline.cpp         # load / run
│       │   ├── Scheduler.cpp        # serial topological dispatch
│       │   ├── ComputeContext.cpp   # submit_halide host adapter
│       │   ├── InferenceContext.cpp # returns CPIPE_UNSUPPORTED
│       │   ├── Registry.cpp         # walk __start_cpipe_registry
│       │   └── HalideBufferAdapter.cpp
│       ├── nodes/
│       │   ├── passthrough.cpp
│       │   ├── passthrough.json
│       │   └── passthrough_copy_generator.cpp   # Halide generator
│       ├── server/                  # empty stub (CMakeLists creates lib)
│       └── cli/
│           └── main.cpp             # CLI11 + Pipeline::run
├── apps/
│   └── cli/                         # alias to src/cpipe/cli for naming parity
├── tests/
│   ├── unit/
│   │   ├── test_pixel_format.cpp
│   │   ├── test_buffer_layout.cpp
│   │   ├── test_cpu_buffer.cpp
│   │   ├── test_status.cpp
│   │   ├── test_registry.cpp
│   │   ├── test_pipeline_load.cpp
│   │   ├── test_scheduler_topo.cpp
│   │   └── test_passthrough_node.cpp
│   ├── integration/
│   │   └── test_passthrough_end_to_end.cpp
│   └── fixtures/
│       ├── passthrough.json
│       └── gen_passthrough_input.cpp     # generates the 64×64 RGBA8 gradient
├── schemas/
│   ├── node-v0.1.json
│   └── pipeline-v0.1.json
└── docs/
    ├── architecture.md
    ├── buffer.md
    ├── plugin-sdk.md
    ├── research/
    ├── roadmap.md
    ├── tech.md
    └── phase-00-foundation.md       # THIS FILE
```

Empty target subdirectories carry a placeholder `CMakeLists.txt` so the six-target structure compiles even before there is code in them (PD-9).

---

## 6. Task List

Seven vertical tasks (PD-28). Each ships in dependency order so the repo never enters a half-built state. Sizes per the [task sizing guide](https://github.com/addy-ai/agent-skills/blob/main/skills/planning-and-task-breakdown/SKILL.md): S = 1–2 files; M = 3–5; L = 5–8.

### T1 — Repo Skeleton & CI

**Description.** Initialize the public GitHub repo, write the top-level CMake / vcpkg / preset files, drop in tooling configs, scaffold the six empty targets, and stand up GitHub Actions.

**Acceptance criteria:**
- [x] `github.com/xndcn/cpipe` is public; `LICENSE` (Apache 2.0) and `README.md` (with pre-alpha warning) are at the root.
- [x] `cmake --preset linux-debug && cmake --build --preset linux-debug` succeeds and produces the Phase 0 target skeleton (`cpipe-sdk` is header-only per [`architecture.md` §3](architecture.md#3-native-module-decomposition)).
- [x] `pre-commit run --all-files` passes.
- [ ] GitHub Actions workflow `build-and-test.yml` is green on a placeholder PR.

**Verification:**
- [ ] `gh repo view xndcn/cpipe --json visibility,description,licenseInfo` returns `PUBLIC` + `Apache-2.0`.
- [ ] CI `lint` + `build-debug` + `build-release` jobs all show green badges on the workflow run.

**Dependencies:** None.

**Files likely touched:**
- `LICENSE`, `README.md`, `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `.clang-tidy`, `.pre-commit-config.yaml`
- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`
- `cmake/CompilerOptions.cmake`, `cmake/HalideHelpers.cmake`, `cmake/EmbedJson.cmake`
- `.github/workflows/build-and-test.yml`
- Six `CMakeLists.txt` stubs (`src/cpipe/{core,runtime,sdk,nodes,server,cli}/CMakeLists.txt`)

**Estimated scope:** L (8–12 files; mostly config).

---

### T2 — `cpipe-core` Data Types

**Description.** Implement the core data types from [`buffer.md` §3–§5](buffer.md#3-pixelformat): `PixelFormat`, `BufferLayout`, `BufferKind`, `BufferUsage`, `IBuffer` interface, status codes, and a working `CpuBuffer` backed by `posix_memalign`.

**Acceptance criteria:**
- [x] `PixelFormat` enum holds all 14 v1 entries from `buffer.md §3`.
- [x] `BufferLayout::size_bytes()` returns correct byte count for each `(kind, format, dims, stride)` combination tested.
- [x] `CpuBuffer` lock / unlock pairs survive at least two cycles and yield aligned pointers (verified via assertion in test).
- [x] `IBuffer::sub_view()` returns `nullptr` and logs a warning per [`buffer.md` §11](buffer.md#11-sub-view-not-implemented-in-v1).

**Verification:**
- [x] `ctest -R test_pixel_format` green.
- [x] `ctest -R test_buffer_layout` green.
- [x] `ctest -R test_cpu_buffer` green.
- [x] `ctest -R test_status` green.

**Dependencies:** T1.

**Files likely touched:**
- `include/cpipe/core/{PixelFormat,BufferLayout,BufferUsage,IBuffer,Status}.hpp`
- `src/cpipe/core/{BufferLayout,CpuBuffer}.cpp`
- `tests/unit/test_pixel_format.cpp`
- `tests/unit/test_buffer_layout.cpp`
- `tests/unit/test_cpu_buffer.cpp`
- `tests/unit/test_status.cpp`

**Estimated scope:** M (5 source files + 4 tests).

---

### T3 — Plugin ABI Surface

**Description.** Drop in the full `cpipe_node.h` per [`plugin-sdk.md` §3](plugin-sdk.md#3-c-abi-cpipe_nodeh), the C++ SDK header `sdk.hpp` per [`plugin-sdk.md` §6](plugin-sdk.md#6-c-sdk-cpipesdkhpp), the registration macro `CPIPE_REGISTER_NODE` and Linux-ELF linker-section helpers per [`plugin-sdk.md` §5](plugin-sdk.md#5-registration-cpipe_register_node--linker-section), and a runtime-side `Registry` that walks `__start_/__stop_cpipe_registry`. The `inference` suite host-side returns `CPIPE_UNSUPPORTED` for any call (PD-15).

**Acceptance criteria:**
- [x] `cpipe_node.h` compiles as C99 (verified by a tiny C file in `tests/unit`).
- [x] `sdk.hpp` compiles as C++20 with `-fexceptions` and no warnings under PD-8 flags.
- [x] `CPIPE_REGISTER_NODE` produces a `cpipe_plugin_desc_t` in the `cpipe_registry` section.
- [x] `runtime::Registry::load_builtin_nodes()` finds the test descriptor between `__start_cpipe_registry` and `__stop_cpipe_registry`.
- [x] `host->get_suite("inference", 1)->submit_inference(...)` returns `CPIPE_UNSUPPORTED`.

**Verification:**
- [x] `ctest -R test_registry` green.
- [x] `nm $(find . -name "*.a") | grep cpipe_registry` shows section symbols.

**Dependencies:** T2 (uses status codes; tests need `CpuBuffer`).

**Files likely touched:**
- `include/cpipe/sdk/cpipe_node.h`
- `include/cpipe/sdk/sdk.hpp`
- `include/cpipe/sdk/registry.hpp`
- `include/cpipe/sdk/section.hpp`
- `src/cpipe/runtime/Registry.cpp`
- `tests/unit/test_registry.cpp` (and a tiny `.c` translation unit for the C99 compile check)

**Estimated scope:** M (4 headers + 1 source + 1 test).

---

### Checkpoint A — after T1–T3

- [ ] All three tasks merged; `main` is green.
- [ ] Repo compiles end-to-end on the CI matrix.
- [ ] ABI header reachable from anywhere; one registered descriptor visible in the registry walk.
- [ ] Review: any unexpected library pulled into the dependency closure? Any P0 risk surfaced?

---

### T4 — Runtime: Scheduler + ComputeContext + Halide Adapter

**Description.** Implement the minimum `cpipe-runtime` needed to dispatch one node: a TaskFlow `Executor` (sized to `std::thread::hardware_concurrency() - 1` per [`architecture.md` §4](architecture.md#4-process-and-thread-model)), a `Scheduler` that walks the topo order serially (PD-20), a `ComputeContext` whose `submit_halide` host-side adapts `cpipe_buffer_t*` → `halide_buffer_t*` per [`plugin-sdk.md` §9.1](plugin-sdk.md#91-halide-aot), and an `InferenceContext` returning `CPIPE_UNSUPPORTED`.

**Acceptance criteria:**
- [x] `runtime::Scheduler` walks a topologically sorted node list and calls each node's `process()` in order.
- [x] `ComputeContext::submit_halide("passthrough_copy", in, out)` invokes the AOT entry point and produces correct output on `CpuBuffer` inputs.
- [x] `halide_set_custom_do_par_for` redirects Halide's CPU parallelism into the cpipe `tf::Executor` per [`architecture.md` §4](architecture.md#4-process-and-thread-model).
- [x] `inference->submit(...)` returns `CPIPE_UNSUPPORTED`.

**Verification:**
- [x] `ctest -R test_scheduler_topo` green.
- [x] `ctest -R test_halide_adapter` green (a stand-alone Halide AOT call using a trivial generator).

**Dependencies:** T3.

**Files likely touched:**
- `src/cpipe/runtime/Scheduler.cpp`, `Scheduler.hpp`
- `src/cpipe/runtime/ComputeContext.cpp`, `ComputeContext.hpp`
- `src/cpipe/runtime/InferenceContext.cpp`
- `src/cpipe/runtime/HalideBufferAdapter.cpp`, `.hpp`
- `tests/unit/test_scheduler_topo.cpp`

**Estimated scope:** M (4–5 source files + tests).

---

### T5 — Passthrough Node

**Description.** Author the Halide generator `passthrough_copy` (CPU target only — Vulkan target reserved for P1), wire `add_halide_library()` to compile it into a static archive, write `nodes/passthrough.cpp` (the C++ `Passthrough` class), `nodes/passthrough.json` (manifest), and the `EmbedJson.cmake` step that turns the JSON into a `.cpp` literal per [`plugin-sdk.md` §7.2](plugin-sdk.md#72-embedding-in-the-binary). Register via `CPIPE_REGISTER_NODE`.

**Acceptance criteria:**
- [x] `passthrough_copy_generator.cpp` compiles into a Halide AOT static library.
- [x] `nodes/passthrough.json` validates against `schemas/node-v0.1.json` (Catch2 host-side validator per PD-32).
- [x] `Passthrough::process()` submits the Halide AOT, copies input bytes to output bytes for any `R8G8B8A8_UNORM` `Image2D`.
- [x] The descriptor `com.cpipe.builtin.passthrough` appears in the registry at startup.

**Verification:**
- [x] `ctest -R test_passthrough_node` green.
- [x] Generated manifest `.cpp` literal is byte-identical to source JSON (modulo whitespace canonicalization).

**Dependencies:** T4.

**Files likely touched:**
- `src/cpipe/nodes/passthrough.cpp`
- `src/cpipe/nodes/passthrough.json`
- `src/cpipe/nodes/passthrough_copy_generator.cpp`
- `schemas/node-v0.1.json`
- `cmake/EmbedJson.cmake` (already drafted in T1; finalised here)
- `tests/unit/test_passthrough_node.cpp`

**Estimated scope:** M (3 source files + manifest + schema + test).

---

### T6 — CLI + Pipeline::load

**Description.** Implement `cpipe-cli` (CLI11-based, single `run` subcommand per PD-18). `Pipeline::load` parses pipeline JSON via nlohmann/json, validates against an embedded `pipeline-v0.1.json` schema, topologically sorts, and runs the minimum memory plan (allocates `CpuBuffer` for each intermediate).

**Acceptance criteria:**
- [ ] `cpipe run input.bin -p pipeline.json -o output.bin` exits 0 on the passthrough pipeline.
- [ ] `cpipe run` rejects an invalid pipeline JSON (e.g. unknown node type, dangling edge) with a non-zero exit and a clear error message.

**Verification:**
- [ ] `cpipe run tests/fixtures/passthrough.bin -p tests/fixtures/passthrough.json -o /tmp/out.bin && cmp /tmp/out.bin tests/fixtures/passthrough.bin` succeeds.
- [ ] `cpipe run tests/fixtures/passthrough.bin -p tests/fixtures/invalid_pipeline.json -o /tmp/out.bin` exits non-zero.

**Dependencies:** T5.

**Files likely touched:**
- `src/cpipe/cli/main.cpp`
- `src/cpipe/runtime/Pipeline.cpp`, `Pipeline.hpp`
- `schemas/pipeline-v0.1.json`
- `tests/fixtures/passthrough.json`, `tests/fixtures/invalid_pipeline.json`, `tests/fixtures/gen_passthrough_input.cpp`
- `tests/unit/test_pipeline_load.cpp`

**Estimated scope:** M (6 source files + schemas + tests).

---

### T7 — Integration Test + DoD Smoke

**Description.** Author the single integration test that drives the whole chain — registry walk → Pipeline::load → Scheduler dispatch → ComputeContext::submit_halide → CpuBuffer compare. Run it under both Debug (ASAN+UBSAN) and Release in CI. Tag `v0.1` once green for ≥ 24 hours.

**Acceptance criteria:**
- [ ] `tests/integration/test_passthrough_end_to_end.cpp` programmatically generates a 64×64 RGBA8 gradient input, runs the passthrough pipeline, and verifies byte-identical output.
- [ ] ASAN + UBSAN produce no findings on the integration run.
- [ ] CI green on `main` for ≥ 24 consecutive hours.
- [ ] Tag `v0.1` created and pushed.

**Verification:**
- [ ] `ctest -R test_passthrough_end_to_end` green under both Debug and Release presets.
- [ ] `git tag --list 'v0.1'` returns `v0.1`.
- [ ] GitHub Releases page shows `v0.1` with auto-generated release notes.

**Dependencies:** T6.

**Files likely touched:**
- `tests/integration/test_passthrough_end_to_end.cpp`
- `CHANGELOG.md` (created here at first release if PD-2 is revisited; otherwise release notes live on GitHub Releases only)

**Estimated scope:** S (1 test file + tagging).

---

### Checkpoint B — after T4–T7 (= P0 DoD)

- [ ] DoD verification commands in §10 all pass.
- [ ] CI matrix has been green for ≥ 24 hours.
- [ ] No regressions on the 8–12 unit tests or the 1 integration test.
- [ ] `v0.1` tag is live.

---

## 7. Architecture Notes (P0-specific)

These are P0 implementation specifics that do not warrant a new locked decision but are worth pinning so T1–T7 stay coherent.

- **Linker-section three-platform compatibility (PD-15, [`plugin-sdk.md` §5.3](plugin-sdk.md#53-three-platform-compatibility))**: P0 only ships the Linux ELF variant (`__attribute__((section("cpipe_registry")))` + `__start_/__stop_cpipe_registry`). The `section.hpp` header guards the macOS Mach-O and Windows COFF branches behind `#ifdef`s but does not compile them in P0. v1.1 turns on macOS; Windows is not v1 ([RD-12-cross-ref Q12 resolved no](roadmap.md#1-decision-quick-reference)).
- **Halide AOT generator stub**: `passthrough_copy_generator.cpp` is a one-class Halide generator that reads one `Buffer<uint8_t>` and writes a copy. The generator is built at configure time by `add_halide_library()`; the runtime mmaps the resulting `.o`. P0 uses CPU target only; Vulkan target is enabled by `cmake -DCPIPE_ENABLE_HALIDE_VULKAN=ON` in P1.
- **`halide_buffer_t` adapter**: `runtime/HalideBufferAdapter.cpp` constructs a `halide_buffer_t` from a `CpuBuffer` (host pointer + dims + strides + element type). The adapter is host-only and is **not** exposed to plugins (PD-15 / B5 / P14).
- **`ComputeContext::submit_halide` resolution**: P0 maintains a string-keyed `std::unordered_map<std::string, halide_filter_entry_t*>` populated at startup with each Halide AOT symbol the runtime knows about. P1 expands this with device variants and a precision planner.
- **Pipeline JSON schema scope**: `schemas/pipeline-v0.1.json` validates `{$schema, version, id, nodes[{id,type,params}], edges[{from,to}]}` and rejects unknown fields. JSON Schema 2020-12 dialect; consumed by both the host (`nlohmann/json-schema-validator`) and (in P3) the Editor (`Ajv`).
- **Compiler options helper**: `cmake/CompilerOptions.cmake` defines a `cpipe_target_warning_flags(<target>)` function. Every target in the project calls it; this localises the warning policy and avoids per-CMakeLists.txt drift.

---

## 8. Tests in P0 (unit + integration)

| # | Test                                  | Layer       | Asserts                                                                                                             |
|---|---------------------------------------|-------------|---------------------------------------------------------------------------------------------------------------------|
| 1 | `test_pixel_format`                   | unit        | each of the 14 `PixelFormat` values has correct bytes-per-pixel and `to_string()`                                   |
| 2 | `test_buffer_layout`                  | unit        | `size_bytes()` matches hand-computed value for `Image2D` / `Volume3D` / `TensorND` / `Blob`                         |
| 3 | `test_buffer_usage`                   | unit        | flag combinations bitwise behave as expected                                                                        |
| 4 | `test_status`                         | unit        | every `cpipe_status_t` has a `to_string()` and round-trips                                                          |
| 5 | `test_cpu_buffer`                     | unit        | lock / unlock / flush; alignment; double-unlock asserted; size matches layout                                       |
| 6 | `test_registry`                       | unit        | startup walk finds the passthrough descriptor; bad ABI version skipped with warning                                 |
| 7 | `test_pipeline_load`                  | unit        | valid pipeline loads; invalid (unknown type, dangling edge, missing port) rejected with clear error                 |
| 8 | `test_scheduler_topo`                 | unit        | topological order honoured; cycle rejected at load                                                                  |
| 9 | `test_halide_adapter`                 | unit        | `CpuBuffer` → `halide_buffer_t` adapter produces correct dims/strides; output matches reference                     |
| 10 | `test_passthrough_node`              | unit        | `Passthrough::process()` produces byte-identical output for a 16×16 gradient                                        |
| 11 | `test_abi_c_compile` (`.c` TU)       | unit        | `cpipe_node.h` compiles cleanly as C99 (no C++-only constructs)                                                     |
| 12 | `test_passthrough_end_to_end`        | integration | registry → load → scheduler → submit_halide → output; ASAN+UBSAN clean; bytes match                                 |

Targets 11 of the "8–12 unit + 1 integration" guidance from PD-26. If any test grows unwieldy during T7, split it; the count is approximate, not a contract.

---

## 9. Risk Register (P0-only)

Per PD-28 plus the slip-absorption posture from [`roadmap.md` §9](roadmap.md#9-slip-absorption-and-scope-pressure-valves), P0 carries three concrete risks. Inherited risks (e.g. R12 TaskFlow / Vulkan integration) are noted but tracked in `research/00-summary.md §7`.

| #     | Risk                                                                                                                                       | Impact | Likelihood | Mitigation                                                                                                                                                                |
|-------|--------------------------------------------------------------------------------------------------------------------------------------------|--------|------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| P0-R1 | Halide v21 FetchContent first build is slow (~150 MB source + LLVM dependencies; estimated 5–15 min cold).                                  | Medium | Confirmed  | vcpkg binary cache + ccache (PD-13) absorb subsequent runs. README documents the first-build expectation. CI cold-build budget: ≤ 20 min.                                  |
| P0-R2 | TaskFlow v4.0.0 header-only uses C++20 `atomic::wait` / `atomic::notify_*`; older libstdc++ / libc++ may lack `_Atomic_wait` primitives.    | Medium | Medium     | CMake checks for `__cpp_lib_atomic_wait >= 201907L`; if absent, fail fast with a clear error pointing at compiler upgrade.                                                  |
| P0-R3 | `.clang-tidy` initial run produces dozens of warnings against `nlohmann/json` / `spdlog` headers; PD-22 turns 20+ checks on simultaneously. | Low    | High       | `.clang-tidy` config restricts checks to first-party sources via `HeaderFilterRegex: cpipe/`. CI lint job exits non-zero only on first-party findings.                       |

The roadmap's overall risk register ([Research 00 §7](research/00-summary.md#7-risk-register)) is unchanged by P0; P0 simply does not exercise R1–R15 directly. R12 (TaskFlow + Vulkan integration) is first exercised in P1 when device-plane buffers appear.

---

## 10. Definition of Done (verification commands)

Run these in order from a fresh clone of `github.com/xndcn/cpipe`. Each command's exit status is the gate.

```bash
# 1. Clone + bootstrap
git clone https://github.com/xndcn/cpipe.git
cd cpipe
pre-commit install

# 2. Configure + build (Debug)
cmake --preset linux-debug
cmake --build --preset linux-debug -j

# 3. Run all tests under Debug (ASAN + UBSAN)
ctest --preset linux-debug --output-on-failure

# 4. Configure + build (Release)
cmake --preset linux-release-clang
cmake --build --preset linux-release-clang -j

# 5. Run all tests under Release
ctest --preset linux-release-clang --output-on-failure

# 6. End-to-end smoke test
./build/linux-release-clang/src/cpipe/cli/cpipe run \
    tests/fixtures/passthrough.bin \
    -p tests/fixtures/passthrough.json \
    -o /tmp/cpipe_p0_out.bin
cmp /tmp/cpipe_p0_out.bin tests/fixtures/passthrough.bin

# 7. CI status
gh run list --workflow=build-and-test.yml --branch=main --limit=5
#    => All five most-recent runs on main must be "completed success".

# 8. Tag
git tag -a v0.1 -m "cpipe v0.1 — Foundation"
git push origin v0.1
```

If commands 1–7 all return zero exit status and CI has been green for ≥ 24 hours, P0 is done.

---

## 11. Dependencies (vcpkg.json baseline)

P0 vcpkg manifest (PD-10):

```json
{
  "name": "cpipe",
  "version-string": "0.1.0-alpha",
  "builtin-baseline": "<pinned-microsoft-vcpkg-commit>",
  "dependencies": [
    "taskflow",
    "catch2",
    "spdlog",
    "nlohmann-json",
    "nlohmann-json-schema-validator",
    "tl-expected",
    "cli11"
  ]
}
```

Halide is pulled via `FetchContent` because vcpkg lags Halide's release cadence (per [`tech.md` §2](tech.md#2-layer-1--build--toolchain)).

The baseline commit is selected when T1 lands and recorded in PD-10 history; subsequent phases bump it deliberately under [`roadmap.md` §11 dependency hygiene](roadmap.md#11-cross-phase-concerns).

---

## 12. Out of Scope (P0)

Stated explicitly so contributors don't accidentally expand P0:

- DNG ingest, OpcodeList interpreter, any real ISP node (P1).
- libheif / kvazaar / OCIO / lcms2 — no color or output (P1).
- VMA, Vulkan-Headers, Vulkan-Loader, any GPU `IBuffer` backend (P1).
- Slang, slang-rhi (P1+).
- ExecuTorch / ONNX Runtime / QAIRT (P4 / v1.1).
- Tracy (P1+).
- macOS / Windows / Android build targets (v1.1 / v1.2 / out of scope).
- Web Editor (P3).
- IQA harness, Google Benchmark, 50-image corpus (P3).
- Git LFS (P1+).
- Any node beyond `com.cpipe.builtin.passthrough`.

---

## 13. See Also

- [`roadmap.md`](roadmap.md) — overall phase plan, RD-NN decisions.
- [`architecture.md`](architecture.md) — six-target layout, threading model, lifecycle.
- [`buffer.md`](buffer.md) — `IBuffer` / `PixelFormat` / `BufferLayout` definitions implemented in T2.
- [`plugin-sdk.md`](plugin-sdk.md) — `cpipe_node.h`, `sdk.hpp`, `CPIPE_REGISTER_NODE` implemented in T3.
- [`tech.md`](tech.md) — license verdicts and version pins for the eight P0 dependencies.
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — full scheduler design (P0 implements the serial subset).
- [`research/10-plugin-architecture.md`](research/10-plugin-architecture.md) — the OpenFX-inspired ABI rationale.
