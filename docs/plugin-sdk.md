# cpipe Plugin SDK

> Date: 2026-05-08 · Companion: [`buffer.md`](buffer.md) · Research: [`research/10-plugin-architecture.md`](research/10-plugin-architecture.md)

cpipe nodes are **plugins**: built-in v1 nodes and external v2 `.so` plugins use the same C ABI plus C++ SDK. This document defines `cpipe_node.h` (the C ABI boundary), `cpipe/sdk.hpp` (the C++ wrapper), the `CPIPE_REGISTER_NODE` registration mechanism, the JSON manifest schema, the node lifecycle, the protocols for submitting Halide / Slang / Inference work, and v1 scope limits. **Buffer types and producer classes are not in this document** — see [`buffer.md`](buffer.md).

---

## 1. Decision Summary

| # | Decision | Note |
|---|----------|------|
| P1 | C ABI follows OpenFX style (opaque handles + suite negotiation + action dispatch) verbatim | Per the research 10 §4.1 sketch |
| P2 | v1 has three suites: buffer / compute / param; inference is a separate fourth suite | Logging is a function pointer on `cpipe_host_t`, not its own suite |
| P3 | Native handles (VkBuffer / AHardwareBuffer / MTLTexture) are **not** in the C ABI; plugins submit work through `ComputeContext` only | Keeps the door open for v2 sandboxing |
| P4 | ABI version = (major, minor) SemVer; the v1 starting number is **v0.1 (alpha)**; pre-1.0 may break | Major = breaking; minor = additive (suite addition) |
| P5 | Plugin registration: `__attribute__((section("cpipe_registry")))` plus linker `__start_/__stop_` symbols | Linux ELF, macOS Mach-O, and Windows COFF are all supported |
| P6 | Manifest = JSON Schema 2020-12; authored as a standalone `node.json` file embedded into the binary at build time via `#include_bin`; at runtime it is a string literal | Validation: `nlohmann/json-schema-validator` host-side, `Ajv` in the editor |
| P7 | Lifecycle: 5 actions — `describe / create / destroy / prepare / process` | No `param_changed` |
| P8 | `ParamView` is an immutable snapshot (`process` sees one); param types: scalar / curve / color; flat keys | Curve = pair of float arrays; color = `RGBA float[4]` |
| P9 | `process()` concurrency: different nodes may run concurrently; a single instance is never reentered | TaskFlow worker pool + per-instance lock |
| P10 | Per-instance state: created in the `create` action and exposed as `void*`; BM3D LUTs, Mertens pyramids, etc. live there | Released in the `destroy` action |
| P11 | Errors: C ABI returns status codes (`CPIPE_OK / FAILED / ...`); the C++ SDK uses Result-style; **never** throw across the ABI | Exceptions stop at the SDK shim |
| P12 | Plugins do not link inference SDKs (ExecuTorch / ONNX RT / QNN / Core ML); the host owns them | `submit_inference("model_id", ins, outs)` |
| P13 | Slang shaders ship as a multi-backend bundle blob (`.slangbin`, produced by build-time `slangc -profile spirv,metal,wgsl`) | Runtime selects the best matching backend |
| P14 | Halide AOT–to-`IBuffer` adaptation (the `halide_buffer_t` glue) is the host's job; plugins never see `halide_buffer_t` | `submit_halide("aot_id", ins, outs)` |
| P15 | The manifest declares static `in_pixels / out_pixels / scratch_pixels` (per-pixel memory cost) so the scheduler can plan | Per research 03 §6 |
| P16 | v1 limits: same-process only; no tile-based / hot-reload / sandboxing; no marketplace | The architecture does not block v2 |

---

## 2. Architecture Overview

```
+-----------------------------------------------+
|  cpipe runtime (host)                         |
|                                               |
|  +--------------------------------------+     |
|  | Plugin Registry                      |     |
|  | (linker section walked at startup)   |     |
|  +----------+---------------------------+     |
|             v                                 |
|  +--------------------------------------+     |
|  | NodeIR (per pipeline.json instance)  |     |
|  | + manifest + cpipe_main_entry        |     |
|  +----------+---------------------------+     |
|             v (action="process")              |
|  +--------------------------------------+     |
|  | cpipe_main_entry (plugin code)       |     |
|  |   uses cpipe_buffer_suite_v1         |     |
|  |   uses cpipe_compute_suite_v1        |     |
|  |   uses cpipe_param_suite_v1          |     |
|  |   uses cpipe_inference_suite_v1      |     |
|  +----------+---------------------------+     |
|             v (suite implementations)         |
|  +--------------------------------------+     |
|  | host-side backends:                  |     |
|  |   Halide AOT runtime                 |     |
|  |   slang-rhi IDevice + IShaderProgram |     |
|  |   ExecuTorch / ONNX RT / QNN / CoreML|     |
|  |   VMA + MTLHeap allocator            |     |
|  |   IFence / ITimeline                 |     |
|  +--------------------------------------+     |
+-----------------------------------------------+
```

The ABI boundary cuts through the middle layer. The plugin's `cpipe_main_entry` only sees opaque handles and suite vtables; the host owns every real resource.

---

## 3. C ABI (`cpipe_node.h`)

The complete v0.1 header. C99-compatible, no STL, no C++ types crossing the boundary.

```c
/* cpipe_node.h — stable C ABI for cpipe nodes. v0.1 (alpha). */
#ifndef CPIPE_NODE_H
#define CPIPE_NODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Version ---------------- */

#define CPIPE_ABI_MAJOR  0
#define CPIPE_ABI_MINOR  1

/* ---------------- Status codes ---------------- */
/* Numeric values are stable forever; never reorder; only append. */
typedef enum {
    CPIPE_OK             = 0,
    CPIPE_FAILED         = 1,
    CPIPE_REPLY_DEFAULT  = 2,   /* unknown action — host falls back */
    CPIPE_OOM            = 3,
    CPIPE_BAD_PRECISION  = 4,
    CPIPE_BAD_INDEX      = 5,
    CPIPE_NEED_PARAM     = 6,
    CPIPE_INTERNAL_ERROR = 7,
    CPIPE_UNSUPPORTED    = 8
} cpipe_status_t;

/* ---------------- Action strings ---------------- */
/* The plugin returns CPIPE_REPLY_DEFAULT for unrecognized actions. */
#define CPIPE_ACTION_DESCRIBE      "describe"   /* manifest scrape; no compute */
#define CPIPE_ACTION_CREATE        "create"     /* per-instance state alloc */
#define CPIPE_ACTION_DESTROY       "destroy"
#define CPIPE_ACTION_PREPARE       "prepare"    /* called once after wiring */
#define CPIPE_ACTION_PROCESS       "process"    /* execute the node */

/* ---------------- Opaque handles ---------------- */
typedef struct cpipe_host_s        cpipe_host_t;
typedef struct cpipe_node_s        cpipe_node_t;       /* per-instance state */
typedef struct cpipe_props_s       cpipe_props_t;      /* immutable param view */
typedef struct cpipe_buffer_s      cpipe_buffer_t;     /* IBuffer wrapper */
typedef struct cpipe_compute_s     cpipe_compute_t;    /* ComputeContext */
typedef struct cpipe_inference_s   cpipe_inference_t;  /* InferenceContext */

/* ---------------- Buffer suite v1 ---------------- */
/* Read-only metadata + CPU access (CPU-device nodes only). */
typedef struct {
    int (*get_dims)        (const cpipe_buffer_t*, uint8_t* ndim,
                            uint32_t out_dims[8]);
    int (*get_format)      (const cpipe_buffer_t*, int* /* PixelFormat enum value */);
    int (*get_kind)        (const cpipe_buffer_t*, int* /* BufferKind enum value */);
    int (*get_stride)      (const cpipe_buffer_t*, uint64_t out_stride[8]);
    int (*get_color_role)  (const cpipe_buffer_t*, const char** /* OCIO role */);

    /* CPU access — valid only when the buffer was allocated with CPU_READ/WRITE usage.
     * Blocks until GPU work completes. unlock_cpu must follow. */
    int (*lock_cpu)        (cpipe_buffer_t*, int access /* 0=R,1=W,2=RW */, void** ptr);
    int (*unlock_cpu)      (cpipe_buffer_t*);
    int (*flush_cpu_writes)(cpipe_buffer_t*);
} cpipe_buffer_suite_v1;

/* ---------------- Compute suite v1 ---------------- */
/* Submit Halide AOT or a slang shader; never raw Vulkan / Metal. */
typedef struct {
    /* Halide AOT — host owns the halide_buffer_t adapter. */
    int (*submit_halide)(cpipe_compute_t*,
                         const char* aot_id,
                         const cpipe_buffer_t* const* inputs,  size_t n_in,
                         cpipe_buffer_t*       const* outputs, size_t n_out);

    /* Slang shader — host owns the slang-rhi IDevice + pipeline cache. */
    int (*submit_slang) (cpipe_compute_t*,
                         const char* slang_module_id,
                         const char* entry_point,
                         const cpipe_buffer_t* const* inputs,  size_t n_in,
                         cpipe_buffer_t*       const* outputs, size_t n_out,
                         const void* push_constants, size_t pc_size);

    /* Scratch buffer — host allocates and returns; lifetime = the process call. */
    int (*request_scratch)(cpipe_compute_t*,
                           uint64_t bytes,
                           int      kind /* BufferKind */,
                           cpipe_buffer_t** out);

    /* Profile marker — emitted to Tracy / Perfetto / Chrome trace. */
    void (*record_marker)(cpipe_compute_t*, const char* label);
} cpipe_compute_suite_v1;

/* ---------------- Inference suite v1 ---------------- */
/* Plugins never link ExecuTorch / ONNX RT / QNN / Core ML. */
typedef struct {
    int (*submit_inference)(cpipe_inference_t*,
                            const char* model_id,
                            const cpipe_buffer_t* const* inputs,  size_t n_in,
                            cpipe_buffer_t*       const* outputs, size_t n_out);
} cpipe_inference_suite_v1;

/* ---------------- Param suite v1 ---------------- */
/* All keys are flat ASCII strings. ParamView is an immutable snapshot
 * obtained at process() entry; values do not change mid-call. */
typedef struct {
    int (*get_double) (const cpipe_props_t*, const char* key, double*  out);
    int (*get_int)    (const cpipe_props_t*, const char* key, int64_t* out);
    int (*get_bool)   (const cpipe_props_t*, const char* key, int*     out);
    int (*get_enum)   (const cpipe_props_t*, const char* key, const char** out);
    /* Curve = (xs[n], ys[n]); pointers are valid until process() returns. */
    int (*get_curve)  (const cpipe_props_t*, const char* key,
                       const float** xs, const float** ys, size_t* n);
    int (*get_color)  (const cpipe_props_t*, const char* key, float rgba[4]);
} cpipe_param_suite_v1;

/* ---------------- Host ---------------- */
struct cpipe_host_s {
    uint32_t abi_major;
    uint32_t abi_minor;

    /* Suite negotiation. Returns NULL if (name, version) is not provided.
     * Names: "buffer", "compute", "param", "inference". */
    const void* (*get_suite)(cpipe_host_t* self,
                             const char* suite_name,
                             int         version);

    /* Logging. level: 0=trace, 1=debug, 2=info, 3=warn, 4=error. */
    void  (*log)(cpipe_host_t* self, int level, const char* msg);

    /* Plugin-side metadata allocation. Never use for image data. */
    void* (*alloc)(cpipe_host_t*, size_t);
    void  (*free) (cpipe_host_t*, void*);
};

/* ---------------- Per-action context structs ----------------
 * The void* in_ctx / out_ctx of cpipe_main_entry_t resolves to one of:
 *
 *   describe : in = NULL, out = NULL  (host already has manifest_json)
 *   create   : in = const cpipe_props_t* (initial params),
 *              out = void**           (write per-instance state pointer here)
 *   destroy  : in = void* (per-instance state from create), out = NULL
 *   prepare  : in = cpipe_compute_t* | cpipe_inference_t*, out = NULL
 *   process  : in = const cpipe_process_ctx*, out = NULL
 */
typedef struct {
    cpipe_compute_t*        compute;
    cpipe_inference_t*      inference;            /* NULL for non-AI nodes */
    const cpipe_buffer_t**  inputs;   size_t n_in;
    cpipe_buffer_t**        outputs;  size_t n_out;
} cpipe_process_ctx;

/* ---------------- Plugin entry ---------------- */
typedef int (*cpipe_main_entry_t)(const char*       action,
                                   cpipe_host_t*    host,
                                   cpipe_node_t*    node,
                                   cpipe_props_t*   params,
                                   void*            in_ctx,
                                   void*            out_ctx);

/* ---------------- Plugin descriptor ---------------- */
/* Linked into the cpipe_registry section; one per node type. */
typedef struct {
    uint32_t            abi_major;        /* must equal CPIPE_ABI_MAJOR at load */
    uint32_t            abi_minor;        /* must <= CPIPE_ABI_MINOR at load */
    const char*         node_id;          /* e.g. "com.cpipe.demosaic.amaze" */
    const char*         node_version;     /* "1.0.0" SemVer */
    const char*         manifest_json;    /* embedded; UTF-8 */
    cpipe_main_entry_t  main_entry;
} cpipe_plugin_desc_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* CPIPE_NODE_H */
```

### 3.1 Suite Negotiation

A plugin calls `host->get_suite("compute", 1)` during the `describe` action. The host returns a vtable pointer for that version, or `NULL`. The plugin caches the pointer in its own state and the rest of its actions invoke vtable functions directly — no further negotiation needed.

Adding a new suite (v1.1, v2, …) is additive: old plugins do not know about it and the host never pushes; new plugins querying an old host get `NULL` and fall back to the older path.

### 3.2 Action Strings, Not Enums

OpenFX experience: action strings let new actions be added without breaking the ABI (old plugins return `CPIPE_REPLY_DEFAULT`). An enum could not, because enum values must be strictly reserved.

---

## 4. ABI Versioning

### 4.1 Version Number

- The project starts at **v0.1 alpha**. Pre-1.0 the ABI may break; each break bumps the minor (`0.1 → 0.2`).
- At project v1.0 GA, freeze to `CPIPE_ABI_MAJOR = 1, CPIPE_ABI_MINOR = 0`.
- Post-v1.0: minor bumps add suites (additive); major bumps break.

### 4.2 Loader Rules

When the host loads a plugin:

1. `desc.abi_major == CPIPE_ABI_MAJOR` — must match exactly.
2. `desc.abi_minor <= CPIPE_ABI_MINOR` — older plugins are accepted; newer plugins are rejected.

### 4.3 Per-Suite Versioning

Each suite carries its own version: `get_suite("compute", 1)` and `get_suite("compute", 2)`. The newer one preserves backwards compatibility with the older one. Plugins try the highest version they understand and fall back.

### 4.4 The "Never Break" Promise

Once published, the numeric values of `cpipe_status_t`, the action strings, and the suite names never change. Adding is allowed; renaming and removing are not. This matches OFX, OBS, and VST3 conventions.

---

## 5. Registration: `CPIPE_REGISTER_NODE` + Linker Section

### 5.1 The Macro

```cpp
// cpipe/sdk/registry.hpp
#define CPIPE_REGISTER_NODE(klass, manifest_json_literal)                       \
    namespace { extern "C" {                                                    \
        static int klass##_main_entry(const char*, cpipe_host_t*,               \
                                       cpipe_node_t*, cpipe_props_t*,           \
                                       void*, void*);                           \
        __attribute__((used, section("cpipe_registry")))                        \
        static const cpipe_plugin_desc_t klass##_desc = {                       \
            .abi_major     = CPIPE_ABI_MAJOR,                                   \
            .abi_minor     = CPIPE_ABI_MINOR,                                   \
            .node_id       = klass::ID,                                         \
            .node_version  = klass::VERSION,                                    \
            .manifest_json = manifest_json_literal,                             \
            .main_entry    = &klass##_main_entry                                \
        };                                                                      \
    }}                                                                          \
    /* see §6 — the SDK template for main_entry dispatches into klass methods */
```

### 5.2 Walking the Linker Section

The cpipe runtime walks the section at startup:

```cpp
// cpipe/runtime/registry.cpp
extern "C" {
    extern const cpipe_plugin_desc_t __start_cpipe_registry[];
    extern const cpipe_plugin_desc_t __stop_cpipe_registry[];
}

void load_builtin_nodes() {
    for (auto* d = __start_cpipe_registry; d < __stop_cpipe_registry; ++d) {
        if (d->abi_major != CPIPE_ABI_MAJOR ||
            d->abi_minor >  CPIPE_ABI_MINOR) {
            log_warn("skipping {} (ABI v{}.{})",
                     d->node_id, d->abi_major, d->abi_minor);
            continue;
        }
        registry_register(d);
    }
}
```

### 5.3 Three-Platform Compatibility

| Platform | Section attribute | Range symbols |
|----------|-------------------|---------------|
| Linux ELF (gcc / clang) | `__attribute__((section("cpipe_registry")))` | `__start_cpipe_registry` / `__stop_cpipe_registry` (linker-generated) |
| macOS Mach-O (clang) | `__attribute__((section("__DATA,cpipe_registry")))` | `getsectiondata(...)` runtime API resolves the range |
| Windows COFF (MSVC) | `#pragma section("cpipe_registry$m", read)` + `__declspec(allocate(...))` | `cpipe_registry$a` / `$z` sentinels (per MSVC docs) |

cpipe wraps the three differences in two macros (`CPIPE_SECTION_PUT` / `CPIPE_SECTION_RANGE`) inside `cpipe/sdk/section.hpp`. The same trick is used by Linux kernel `__initcall`, Catch2 test registration, and the protobuf descriptor pool.

### 5.4 v2 External `.so` Loading

Not implemented in v1, but the ABI is ready:

1. `dlopen("plugin.so")`.
2. `dlsym("cpipe_get_descriptor")` — this symbol is auto-generated by the build system in plugin `.so`'s (the same `CPIPE_REGISTER_NODE` macro), returning `const cpipe_plugin_desc_t*`.
3. Use the same registration flow as §5.2.

---

## 6. C++ SDK (`cpipe/sdk.hpp`)

Header-only. Plugin authors write C++ classes; they do not touch the C ABI directly.

```cpp
// cpipe/sdk.hpp — header-only C++ SDK. C++20.
#pragma once
#include "cpipe_node.h"
#include <span>
#include <string_view>
#include <variant>
#include <expected>          // C++23; tl::expected polyfill works too

namespace cpipe::sdk {

// ---- Result type (P11) ----
struct Error { cpipe_status_t code; std::string message; };
template <class T>
using Result = std::expected<T, Error>;

// ---- Buffer (read-only facade over cpipe_buffer_t) ----
class Buffer {
public:
    uint32_t          width()       const noexcept;
    uint32_t          height()      const noexcept;
    uint32_t          depth()       const noexcept;
    int               format()      const noexcept;   // PixelFormat
    int               kind()        const noexcept;   // BufferKind
    std::string_view  color_role()  const noexcept;

    // CPU access — only for CPU-device nodes.
    enum class Access { Read, Write, ReadWrite };
    Result<std::span<std::byte>> lock_cpu(Access);
    void                          unlock_cpu();
    void                          flush_cpu_writes();

private:
    cpipe_buffer_t*              impl_;
    const cpipe_buffer_suite_v1* suite_;
};

// ---- ComputeContext ----
class ComputeContext {
public:
    Result<void> submit_halide(std::string_view aot_id,
                               std::span<const Buffer*> inputs,
                               std::span<Buffer*>       outputs);

    Result<void> submit_slang (std::string_view module_id,
                               std::string_view entry,
                               std::span<const Buffer*> inputs,
                               std::span<Buffer*>       outputs,
                               std::span<const std::byte> push_constants = {});

    Result<Buffer*> request_scratch(uint64_t bytes,
                                    int kind /* BufferKind */);

    void mark(std::string_view label) noexcept;

private:
    cpipe_compute_t*              impl_;
    const cpipe_compute_suite_v1* suite_;
};

// ---- InferenceContext (separate from compute) ----
class InferenceContext {
public:
    Result<void> submit(std::string_view model_id,
                        std::span<const Buffer*> inputs,
                        std::span<Buffer*>       outputs);

private:
    cpipe_inference_t*              impl_;
    const cpipe_inference_suite_v1* suite_;
};

// ---- ParamView (immutable snapshot, P8) ----
class ParamView {
public:
    double                  d(std::string_view key) const;
    int64_t                 i(std::string_view key) const;
    bool                    b(std::string_view key) const;
    std::string_view        e(std::string_view key) const;
    struct Curve { std::span<const float> xs, ys; };
    Curve                   curve(std::string_view key) const;
    std::array<float, 4>    color(std::string_view key) const;

private:
    const cpipe_props_t*         impl_;
    const cpipe_param_suite_v1*  suite_;
};

// ---- Node base class (plugin authors derive from this) ----
class Node {
public:
    virtual ~Node() = default;

    // create-time: build per-instance state (BM3D LUT, Mertens pyramid weights, ...).
    // Default: no-op.
    virtual Result<void> create(const ParamView&) { return {}; }

    // After wiring; can JIT-warm Halide / Slang pipelines.
    virtual Result<void> prepare(ComputeContext&,
                                 InferenceContext*,
                                 const ParamView&) { return {}; }

    // Hot path. Called by the scheduler from any worker thread; the same instance is never reentered.
    virtual Result<void> process(ComputeContext&,
                                 InferenceContext*,
                                 const ParamView&,
                                 std::span<const Buffer*> inputs,
                                 std::span<Buffer*>       outputs) = 0;
};

// ---- dispatch shim — hides cpipe_main_entry_t dispatch from plugin authors ----
namespace detail {
template <class T> int dispatch(const char* action, cpipe_host_t* host,
                                cpipe_node_t* node, cpipe_props_t* params,
                                void* in_ctx, void* out_ctx);
}  // namespace detail

}  // namespace cpipe::sdk
```

### 6.1 The dispatch Shim

`detail::dispatch<T>` is what `CPIPE_REGISTER_NODE` aliases as `klass##_main_entry`. It:

1. Pulls suite pointers from the host (cached once, thread-local).
2. `strcmp`s the action and dispatches to `T::create / destroy / prepare / process`.
3. Catches any C++ exception and converts it into `CPIPE_INTERNAL_ERROR + host->log`. **Exceptions never cross the ABI.**

### 6.2 SDK Mirrors the ABI 1:1

`cpipe::sdk::Buffer / ComputeContext / InferenceContext / ParamView` each holds a `cpipe_*_t* impl_` plus the matching `*_suite_v1*`. Methods forward to the vtable. The wrapper layer is purely ergonomic; the runtime cost is one extra indirect call.

---

## 7. Manifest Schema

### 7.1 Single Source of Truth

Authors write `node.json`:

```json
{
  "$schema": "https://schemas.cpipe.dev/node/v0.1.json",
  "id":      "com.cpipe.denoise.bm3d",
  "version": "1.0.0",
  "label":   "BM3D Denoise",
  "category": "denoise/single-frame",
  "doc":     "Block-matching 3D denoise. CPU-only in v1.",

  "ports": [
    { "name": "in",  "kind": "in",
      "caps": { "buffer_kind": "Image2D",
                "channels": ["rgb"], "precision": ["fp16","fp32"] },
      "cardinality": "single" },
    { "name": "out", "kind": "out",
      "caps": { "buffer_kind": "Image2D",
                "channels": ["rgb"], "precision": ["fp16"] },
      "cardinality": "single" }
  ],

  "params": [
    { "name": "sigma",     "type": "float", "default": 3.0,
      "min": 0.0, "max": 50.0, "step": 0.1, "label": "Noise std.dev." },
    { "name": "profile",   "type": "enum",
      "choices": ["high-precision","balanced","fast"], "default": "balanced" },
    { "name": "luma_only", "type": "bool",  "default": false }
  ],

  "compute": {
    "device":         "CPU",
    "engine":         "Halide",
    "halide_aot":     ["bm3d_cpu_avx2","bm3d_cpu_neon"],
    "in_pixel_bytes":  8,
    "out_pixel_bytes": 8,
    "scratch_pixel_bytes": 64
  },

  "color": {
    "input_role":  "scene_linear",
    "output_role": "scene_linear",
    "respects_chromaticity": true
  },

  "test": {
    "golden_pairs": [
      { "in": "test/golden/bm3d/in_001.exr",
        "out": "test/golden/bm3d/out_001.exr" }
    ],
    "tolerance_psnr_db": 50.0,
    "rng_seed": 42
  }
}
```

### 7.2 Embedding in the Binary

CMake embeds `node.json` into the binary at build time using `#include_bin` (or an `add_custom_command` that generates a `.cpp` literal):

```cmake
# CMakeLists.txt excerpt
add_custom_command(OUTPUT bm3d_manifest.cpp
  COMMAND ${CMAKE_COMMAND} -P ${CPIPE_DIR}/cmake/EmbedJson.cmake
          ${CMAKE_CURRENT_SOURCE_DIR}/bm3d.json
          ${CMAKE_CURRENT_BINARY_DIR}/bm3d_manifest.cpp
          BM3D_MANIFEST_JSON
  DEPENDS bm3d.json)
target_sources(node_bm3d PRIVATE bm3d_manifest.cpp bm3d.cpp)
```

`bm3d_manifest.cpp` is generated as:

```cpp
extern const char BM3D_MANIFEST_JSON[];
const char BM3D_MANIFEST_JSON[] = "{\n\"id\":\"com.cpipe.denoise.bm3d\",...}";
```

`bm3d.cpp`:

```cpp
extern const char BM3D_MANIFEST_JSON[];
CPIPE_REGISTER_NODE(BM3D, BM3D_MANIFEST_JSON)
```

### 7.3 Validation

- **Host (local)**: `nlohmann/json` (MIT) + `nlohmann/json-schema-validator` (MIT). The schema lives at `cpipe/schemas/node-v0.1.json` and is compiled into the binary.
- **Editor (web)**: `Ajv` (MIT). The cpipe runtime serves the schema at `GET /api/schemas/node`.
- **CI**: a `pre-commit` hook runs the Ajv CLI against every `node.json`; commits that fail validation are rejected.

### 7.4 Field Reference

| Field | Meaning | Required |
|-------|---------|----------|
| `id` | Inverse-DNS unique identifier (`com.cpipe.<category>.<algo>`) | yes |
| `version` | SemVer; must match `cpipe_plugin_desc_t::node_version` | yes |
| `ports` | Input / output ports; `cardinality` ∈ `single` / `array` (Burst uses `array`) | yes |
| `params` | Parameter declarations; types restricted to `float / int / bool / enum / curve / color` | no |
| `compute.device` | `CPU / Vulkan / Metal / Hexagon / NPU / AppleNeuralEngine` | yes |
| `compute.engine` | `Halide / Slang / Inference` | yes |
| `compute.in_pixel_bytes` etc. | Memory-cost estimate for the scheduler (P15) | yes |
| `color` | OCIO role (advisory; transforms happen in dedicated nodes) | yes (use `"any"` if irrelevant) |
| `test.golden_pairs` | CI input (research 09 IQA harness) | no |

---

## 8. Lifecycle Actions

### 8.1 describe

- Called by the cpipe runtime once per `cpipe_plugin_desc_t` at startup.
- Returns `CPIPE_OK` or `CPIPE_REPLY_DEFAULT` (the latter is fine because `manifest_json` is already embedded in the descriptor).
- Use case: rare nodes that need to load external resources at describe time (e.g. dynamic OCIO role lookup). v1 built-ins all return `REPLY_DEFAULT`.

### 8.2 create

- Called when the pipeline loads and the node is instantiated; once per instance.
- `in_ctx`: `const cpipe_props_t*` (initial parameters, immutable).
- `out_ctx`: `void**` — the plugin writes its per-instance state pointer here (P10). BM3D's 7×7 LUT, the Mertens pyramid weights, etc. are budgeted here.
- SDK template: invokes `T::create(params)` and stores the resulting `T*` in `out_ctx`.

### 8.3 destroy

- Called when the pipeline tears down.
- `in_ctx`: `void*` per-instance state.
- The plugin releases anything it allocated in `create`. The SDK does `delete T*`.

### 8.4 prepare

- Called once after the topology is frozen but before the first `process`.
- `in_ctx`: `cpipe_compute_t*` (or `cpipe_inference_t*`); the plugin can JIT-warm Halide pipelines, Slang pipeline cache, or load inference models here.
- Optional; default: no-op.

### 8.5 process

- Called on every `Pipeline::run()` when the scheduler dispatches the node.
- `in_ctx`: `const cpipe_process_ctx*` (compute / inference / inputs / outputs).
- Concurrency (P9): the scheduler guarantees that the same instance's `process` is **serialized**; different instances may run concurrently; the calling thread varies (TaskFlow worker pool). Plugins do not need their own per-instance lock; globals shared across instances need explicit locking.
- A single `process` may submit multiple compute calls (e.g. BM3D = match → transform → aggregate, three kernels).

---

## 9. Compute-Backend Submission Protocols

### 9.1 Halide AOT

In `process`:

```cpp
ctx.compute.submit_halide("demosaic_amaze", inputs, outputs);
```

At build time, a Halide generator (`*Generator`) is compiled via `Halide::compile_to_callable / compile_to_static_library` into per-`(target, schedule)` `.a / .o`. Each plugin declares its variants in CMake:

```cmake
add_halide_library(demosaic_amaze
  FROM cpipe::DemosaicAmazeGenerator
  TARGETS x86-64-linux-avx2,x86-64-linux-avx512,arm-64-android,
          host-vulkan,host-metal,arm-64-android-hvx-128)
```

The cpipe runtime mmaps every AOT object at startup. `submit_halide("demosaic_amaze", ...)` then, host-side:

1. Picks the AOT variant matching the active device.
2. Adapts `cpipe_buffer_t*` into `halide_buffer_t*` (host-side adapter, research 03 §10): `device_interface =` Vulkan / Metal / Hexagon (`halide_*_device_interface()`).
3. Invokes `halide_demosaic_amaze(in_buf, out_buf, ...)`.
4. Overrides Halide's CPU thread pool with the cpipe `tf::Executor` (`halide_set_custom_do_par_for`) so Halide does not spawn its own threads in competition with TaskFlow.

**Plugins never see `halide_buffer_t`**. This is what lets v2 sandboxing isolate plugins into a subprocess: the host stays trusted and runs the `halide_buffer_t` adaptation.

### 9.2 Slang Shaders

Build time:

```cmake
add_slang_kernel(hdr_fusion
  SOURCES hdr_fusion.slang
  ENTRY   hdr_fuse
  TARGETS spirv,metal,wgsl)
```

Output: a single `hdr_fusion.slangbin` (P13), a multi-backend SPIR-V / MSL / WGSL archive in cpipe's own simple TLV format (`magic | n_targets | for each: target_tag(8B) + payload_size(4B) + payload`).

In `process`:

```cpp
struct PushConst {
    float ev_bias;
    float saturation_clip;
} pc{ params.d("ev_bias"), params.d("saturation_clip") };

ctx.compute.submit_slang("hdr_fusion", "hdr_fuse",
                         inputs, outputs,
                         std::as_bytes(std::span<PushConst>(&pc, 1)));
```

Push-constants are limited to 256 B (Vulkan minimum guarantee). For larger parameters, use a uniform buffer (slang `[[vk::binding(0,1)]] cbuffer ...`) backed by a `request_scratch` of `BufferKind::Blob`.

The host-side `submit_slang`:

1. Looks up `(module, entry, target)` in the slang-rhi `IShaderProgram` cache; compiles + caches on miss.
2. Creates an `IShaderObject`, binds buffers and push-constants.
3. `dispatchCompute(grid_x, grid_y, grid_z)` (the grid is derived from the output buffer's dims).
4. Submits onto the cpipe-owned `ICommandQueue`, ordered by timeline value.

### 9.3 Inference

In `process`:

```cpp
ctx.inference->submit("nafnet_w32_int8_qnnhtp_v75",
                       inputs, outputs);
```

`model_id` is a string declared in the manifest. At load time the host has already:

- Resolved `nafnet_w32_int8_qnnhtp_v75.{pte,onnx,mlmodel}` to the right ExecuTorch / ONNX RT / Core ML / QNN backend.
- Pre-warmed the runtime, set up `Qnn_Tensor_t` / `MLMultiArray` input/output bindings.
- Plugins **do not link** any inference SDK; ExecuTorch / ONNX RT / QNN / Core ML are linked statically or dynamically by the host.

Cross-device handoff (Vulkan→HTP) is performed inside `submit_inference` by the scheduler; the plugin only sees buffers in / buffers out. See [`research/05-npu-backends-zero-copy.md`](research/05-npu-backends-zero-copy.md).

---

## 10. Error Handling (Result-style)

### 10.1 Across the ABI

The C ABI returns `cpipe_status_t`. Plugins **never** let exceptions cross the boundary.

### 10.2 Inside the SDK

The C++ SDK uses `Result<T> = std::expected<T, Error>` (C++23; a polyfill works too):

```cpp
Result<void> process(ComputeContext& cc, ...) override {
    if (auto r = cc.submit_halide("demosaic_amaze", in, out); !r) {
        return std::unexpected(r.error());
    }
    return {};
}
```

### 10.3 Disambiguating from Logging

`host->log(level, msg)` is for diagnostic output (Tracy span name, Perfetto trace, debug). `Result / Error` is for "this operation failed; tell me about it". Both are used in tandem.

### 10.4 Unrecoverable Errors

`CPIPE_INTERNAL_ERROR` indicates a plugin bug (null pointer, etc.). Seeing it, the host aborts the entire pipeline run; the plugin should not try to retry on its own.

---

## 11. Node Examples

### 11.1 Passthrough (unit-test fixture)

```cpp
// nodes/passthrough.cpp
#include "cpipe/sdk.hpp"

namespace cpipe::nodes {

class Passthrough : public sdk::Node {
public:
    static constexpr const char* ID      = "com.cpipe.builtin.passthrough";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& cc,
                              sdk::InferenceContext*,
                              const sdk::ParamView&,
                              std::span<const sdk::Buffer*> in,
                              std::span<sdk::Buffer*>       out) override {
        return cc.submit_halide("passthrough_copy", in, out);
    }
};

}  // namespace cpipe::nodes
```

```cpp
extern const char PASSTHROUGH_MANIFEST_JSON[];   // generated by CMake
CPIPE_REGISTER_NODE(cpipe::nodes::Passthrough, PASSTHROUGH_MANIFEST_JSON)
```

### 11.2 Halide Demosaic

```cpp
// nodes/demosaic_amaze.cpp
class DemosaicAmaze : public sdk::Node {
public:
    static constexpr const char* ID      = "com.cpipe.demosaic.amaze";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& cc,
                              sdk::InferenceContext*,
                              const sdk::ParamView& p,
                              std::span<const sdk::Buffer*> in,
                              std::span<sdk::Buffer*>       out) override {
        cc.mark("demosaic_amaze");
        return cc.submit_halide("demosaic_amaze", in, out);
    }
};
CPIPE_REGISTER_NODE(cpipe::nodes::DemosaicAmaze, DEMOSAIC_AMAZE_MANIFEST_JSON)
```

Manifest highlights: `compute.engine = "Halide"`, `compute.halide_aot = ["demosaic_amaze_avx2", "demosaic_amaze_neon", "demosaic_amaze_vulkan", ...]`.

### 11.3 Slang HDR Fusion

```cpp
// nodes/hdr_fusion.cpp
class HdrFusion : public sdk::Node {
public:
    static constexpr const char* ID      = "com.cpipe.hdr.fusion";
    static constexpr const char* VERSION = "1.0.0";

    struct PushConst { float ev_bias; float sat_clip; };

    sdk::Result<void> process(sdk::ComputeContext& cc, sdk::InferenceContext*,
                              const sdk::ParamView& p,
                              std::span<const sdk::Buffer*> in,
                              std::span<sdk::Buffer*>       out) override {
        PushConst pc{ float(p.d("ev_bias")),
                      float(p.d("saturation_clip")) };
        return cc.submit_slang(
            /* module */ "hdr_fusion",
            /* entry  */ "hdr_fuse",
            in, out,
            std::as_bytes(std::span<PushConst>(&pc, 1))
        );
    }
};
CPIPE_REGISTER_NODE(cpipe::nodes::HdrFusion, HDR_FUSION_MANIFEST_JSON)
```

### 11.4 NAFNet AI Denoise (inference)

```cpp
// nodes/ai_denoise_nafnet.cpp
class AiDenoiseNafnet : public sdk::Node {
public:
    static constexpr const char* ID      = "com.cpipe.ai.denoise.nafnet_w32";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext&  cc,
                              sdk::InferenceContext* inf,
                              const sdk::ParamView& p,
                              std::span<const sdk::Buffer*> in,
                              std::span<sdk::Buffer*>       out) override {
        cc.mark("nafnet_inference");
        return inf->submit("nafnet_w32_int8", in, out);
    }
};
CPIPE_REGISTER_NODE(cpipe::nodes::AiDenoiseNafnet, NAFNET_MANIFEST_JSON)
```

Manifest highlights: `compute.engine = "Inference"`, `compute.device = "NPU"` (with `Vulkan` as a fallback). The scheduler picks the right model variant by device availability at planning time.

---

## 12. v1 Scope Limits

Stated explicitly to prevent misuse:

- **Same-process only**: plugins share the host's address space. The C ABI is shaped to permit v2 cross-process operation, but v1 does not implement it (no `SCM_RIGHTS`, no Cap'n Proto bridge).
- **No hot-reload**: plugin registration walks once at startup. v2 will support `dlopen` + descriptor re-walk.
- **No sandboxing**: plugin trust = cpipe's internal security boundary.
- **No marketplace**: v1 has built-ins only; signing / pinned public keys (Ed25519) is a v2 topic.
- **No dynamic DAG**: D6 mandates static topology; a plugin must not build sub-pipelines from inside `process` (`Pipeline::sub_pipeline()` is a v2 topic).
- **No streaming**: D5 says batch only; token-based pipelines (`tf::Pipeline`) are reserved for v2.
- **No tile-based processing**: D2; a single `IBuffer` is the entire image.
- **No `param_changed` action**: parameter changes restart the pipeline run; BM3D-style precomputation tables are recomputed every `process` (the manifest may declare `cache_per_run` for the host to dedupe automatically).

These limits do not block v2: the ABI and manifest schema already reserve extension points (suite negotiation, new action strings, reserved fields).

---

## 13. Relationship with `buffer.md`

| Topic | Defined in `buffer.md` | Defined here |
|-------|------------------------|--------------|
| `IBuffer` interface | yes | — |
| `BufferLayout` / `PixelFormat` / `BufferKind` | yes | — (this doc references the enum values) |
| Concrete backend types (`VulkanBuffer`, etc.) | yes | — |
| Allocator (VMA / MTLHeap) | yes | — |
| External imports (AHB / IOSurface / host pointer) | yes | — |
| `IFence` / `ITimeline` | yes (host-only) | — |
| `Camera2BufferProducer` / `DngReader` | yes | — |
| `cpipe_buffer_t` opaque + `buffer_suite_v1` | — | yes |
| `cpipe_node.h` complete ABI | — | yes |
| `sdk.hpp` (`Buffer` / `ComputeContext` / `Node`) | — | yes |
| `CPIPE_REGISTER_NODE` and linker-section trick | — | yes |
| Manifest schema | — | yes |
| Node examples | — | yes |

The pipeline JSON (the Editor-produced, CLI-consumed `pipeline.cpipe.json` graph description) is owned by [`research/11-pipeline-editor-and-connectivity.md`](research/11-pipeline-editor-and-connectivity.md); this document only references it.

---

## 14. See Also

- [`buffer.md`](buffer.md) — IBuffer / Allocator / Producer / synchronization primitives.
- [`research/10-plugin-architecture.md`](research/10-plugin-architecture.md) — the full design rationale, drawing from OpenFX / OBS / VST3 / ComfyUI.
- [`research/01-compute-frameworks.md`](research/01-compute-frameworks.md) — Halide / Slang / slang-rhi selection and the `ComputeContext` concept.
- [`research/03-heterogeneous-scheduler.md`](research/03-heterogeneous-scheduler.md) — TaskFlow + device plane; the concurrency model for `process()`.
- [`research/04-mobile-ai-inference.md`](research/04-mobile-ai-inference.md) — what `InferenceContext` resolves to (ExecuTorch / ONNX RT).
- [`research/05-npu-backends-zero-copy.md`](research/05-npu-backends-zero-copy.md) — how `submit_inference` reaches Hexagon HTP / ANE in detail.
- [`research/09-image-quality-benchmarks.md`](research/09-image-quality-benchmarks.md) — the IQA harness that consumes `manifest.test.golden_pairs`.
