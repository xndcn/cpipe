# Report 10 — Plugin Architecture

> Cluster D · Owner: Plugin & Pipeline Editor sub-agent · Date: 2026-05-08
> Companion: [#11 — Pipeline Editor & Connectivity](11-pipeline-editor-and-connectivity.md)

## 1. TL;DR

cpipe needs a node plugin model that survives a 12-month v1 (built-in only) and a v2 (third-party `.so`) without being redesigned. The right shape is the one OpenFX, OBS, and GStreamer converged on: a thin **C ABI** at the boundary, a fat **C++ SDK** layered on top, and a **JSON manifest** that describes parameters, ports, precision, and color-space invariants so the host can introspect a plugin without loading code. ComfyUI and its `INPUT_TYPES`/`RETURN_TYPES` reflexion-via-classmethod gives the right ergonomics for declaring nodes, but its Python implementation is irrelevant to a native C++ pipeline; what we keep is the *declarative-first* posture. OpenFX (BSD-3, [v1.5.1, Nov 2025](https://github.com/AcademySoftwareFoundation/openfx)) is the closest precedent: opaque handles, suite tables, property sets, parameter introspection, and a stable C boundary. cpipe v1 ships **internal nodes registered through the same C entry point** that v2 third-party plugins will call; this guarantees ABI cleanliness (per D4) without paying any runtime cost — built-in nodes are resolved at link time and the registration table is `constexpr`. Each node's manifest declares per-port precision (per D9), allowing the scheduler in [#03](03-heterogeneous-scheduler.md) to insert minimum FP16↔FP32 conversions. Compute access is mediated by a `ComputeContext` from [#01](01-compute-frameworks.md); buffers are `cpipe::Buffer` from [#02](02-zero-copy-buffer-architecture.md). Plugins ship Halide AOT objects, slang shaders, or quantized `.pte` weights ([#04](04-mobile-ai-inference.md)) — the host never sees Vulkan/Metal directly. Test harness uses golden-image regression per node, with seeds and tolerance baked into the manifest.

## 2. Decision matrix

### 2.1 Plugin ABI surface

| Option | Stability | C++ ergonomics | Reflection cost | Picked? |
|--------|-----------|----------------|-----------------|---------|
| C ABI + opaque handles + suite tables (OFX-style) | Excellent — backwards-compat across SDK 1.2→1.5 since 2004 | Good with C++ wrapper | Low (manifest declares schema) | **Yes** |
| C++ ABI directly | Brittle — any `std::string` change breaks ABI | Best | None | No |
| C ABI + JSON-RPC over UDS | Survives compiler changes | Acceptable | Low | No (latency, complexity) |
| Python embedding (ComfyUI) | N/A — interpreter overhead | N/A | Built-in | No (D11/D14) |
| Lua embedding (Lightroom legacy) | OK but limited types | Adequate | None | No (limited GPU access) |

### 2.2 Manifest format

| Option | Schema-evolvable | Tooling | Picked? |
|--------|------------------|---------|---------|
| **JSON Schema (Draft 2020-12) + manifest per node** | Yes (additive) | Ajv, jsonschema | **Yes** |
| TOML | Yes | toml++ | No (less tooling) |
| YAML | Yes | rapidyaml | No (anchors complicate diffing) |
| Embedded `RETURN_TYPES`/`INPUT_TYPES` style | Hard to introspect from outside the runtime | None | No (forces loading code) |

### 2.3 Compute access

| Option | Plugin sees | Justification | Picked? |
|--------|-------------|---------------|---------|
| **`ComputeContext` (host-mediated)** | Submit Halide pipeline / slang shader / inference graph | Plugin remains compute-backend agnostic | **Yes** |
| Plugin sees Vulkan/Metal directly | Raw API | Power-user backdoor | No (locks plugin to one backend) |
| Plugin sees CUDA/ROCm directly | Vendor API | Pre-optimized kernels | No (D18 portability) |

### 2.4 Internal vs external loading

| When | Mechanism |
|------|-----------|
| **v1 (built-in)** | Static registration via `CPIPE_REGISTER_NODE(...)` — same entry table as v2 |
| **v2 (third-party)** | `dlopen`/`LoadLibrary` of `.so`/`.dylib`/`.dll`; calls same C entry; manifest signed |

## 3. Detailed findings

### 3.1 ComfyUI — what to keep, what to drop

ComfyUI ([Comfy-Org/ComfyUI on GitHub](https://github.com/Comfy-Org/ComfyUI), GPLv3, accessed 2026-05-08) is the user's stated inspiration. Its node model has three observable strengths:

1. **Declarative classmethod schema.** A node class returns its inputs through `INPUT_TYPES` (a `@classmethod`) and outputs through the `RETURN_TYPES` tuple, with `FUNCTION` naming the dispatch. Example from the [official walkthrough](https://docs.comfy.org/custom-nodes/walkthrough) (accessed 2026-05-08):
   ```python
   @classmethod
   def INPUT_TYPES(s):
       return {"required": {"images": ("IMAGE",),
                            "mode": (["brightest", "reddest", "greenest", "bluest"],)}}
   RETURN_TYPES = ("IMAGE",)
   FUNCTION = "select"
   CATEGORY = "image/select"
   ```
   The frontend can inspect this without executing the node — type strings drive socket colors, dropdowns, validators. cpipe's manifest plays the same role, but as JSON (so the editor on GitHub Pages can read it without a Python interpreter).

2. **Workflow JSON is the ground truth.** The graph itself is JSON: nodes, links, widget values. Anything that can produce that JSON can drive ComfyUI. cpipe inherits this — the editor produces a `pipeline.cpipe.json`, the CLI consumes it, and there is no other source of truth.

3. **Manager + registry is grafted on, not the core.** [ComfyUI-Manager](https://github.com/Comfy-Org/ComfyUI-Manager) reached pyproject.toml-based dependency declaration with the [Nodes V3 schema](https://comfyui.org/en/comfyui-v3-dependency-resolution) in 2026; before that it was URL-based and brittle. cpipe should not repeat the brittle phase: from day one each node is identified by `(authority, name, semver)` even though v1 only resolves to the built-in registry.

What we **drop**: the Python interpreter, dynamic class introspection at runtime, and the [LiteGraph.js](https://github.com/Comfy-Org/litegraph.js) canvas. The licensing matters too — ComfyUI core is GPLv3 (per D11 we cannot link it), so we take only architectural lessons. ComfyUI's [Nodes 2.0 announcement](https://blog.comfy.org/p/comfyui-node-2-0) (Jul 2025) describes Vue-based DOM rendering replacing the canvas, exactly because Canvas2D became the bottleneck — relevant to our editor choice in [#11 §3](11-pipeline-editor-and-connectivity.md).

### 3.2 OpenFX — the closest precedent

OpenFX (OFX), maintained by the [Academy Software Foundation](https://github.com/AcademySoftwareFoundation/openfx), reached **v1.5.1** on 2025-11-20 and is **BSD-3-Clause** (Apache-compatible per [SPDX matrix](https://www.apache.org/legal/resolved.html), accessed 2026-05-08). It has powered Nuke, Resolve, Fusion, Natron, Scratch, and Mistika for two decades. Lessons:

- **Header-only C ABI.** Per the [foreword](https://openfx.readthedocs.io/en/latest/Guide/index.html), "The OFX API is specified using the C programming language purely by a set of header files, with no libraries a plugin needs to link against." cpipe should match this: a single `cpipe_node.h` declares everything; the SDK in C++ is optional sugar.

- **Property sets via opaque handles.** `OfxPropertySetHandle` is a blind pointer that the host populates and the plugin queries through a suite vtable. The plugin never sees a struct layout; only typed getters/setters. This is what makes a 22-year-old plugin still load. cpipe uses an analogous `cpipe_props_t*` opaque handle.

- **Suite negotiation.** A plugin asks the host: "give me the parameter suite v1, the image-effect suite v1, the property suite v1." The host returns a vtable or `nullptr`. Older plugins keep working because new suites are additive. cpipe should ship `cpipe_compute_suite_v1`, `cpipe_buffer_suite_v1`, `cpipe_param_suite_v1` from day one, and reserve `_v2` slots.

- **Action-based dispatch.** The host calls `mainEntry(action, handle, in, out)` with an action string ("kOfxImageEffectActionDescribe", "kOfxActionLoad", "kOfxImageEffectActionRender"). This makes adding new lifecycle hooks ABI-safe — old plugins return `kOfxStatReplyDefault` for unknown actions. cpipe should mirror this with `cpipe_node_action_*` strings.

- **Two-phase description.** A plugin first runs a `Describe` action to declare its parameters and ports, *then* an `DescribeInContext` for the specific host wiring, *then* per-instance creation. This means the host can introspect a plugin without ever rendering. cpipe's manifest does this declaratively, but the node still must register itself through a `Describe`-equivalent for v2 dynamic loading.

- **License for examples.** The reference SDK ships examples (Basic, Invert, Gain, etc.) under BSD; we can copy their structure verbatim into cpipe's SDK reference.

[ofxParameter.h reference](https://openfx.readthedocs.io/en/main/Reference/ofxParameter.html) (accessed 2026-05-08) shows parameter types: Integer, Double, Boolean, Choice, String, Custom, Group, Page, RGBA, RGB, 2D/3D, Pushbutton, ParametricCurve. cpipe's parameter ontology should be a strict subset (no Pushbutton — we have no event model in v1), plus image-specific extras (curve, LUT-3D, color, matrix-3x3).

### 3.3 OBS Studio — clean C registration tables

OBS plugins ([docs.obsproject.com/plugins](https://docs.obsproject.com/plugins), accessed 2026-05-08, OBS 32.1.2) use a registration pattern worth lifting verbatim. The plugin's `obs_module_load()` calls `obs_register_source(&info)` with a struct of function pointers:

```c
struct obs_source_info my_filter = {
    .id           = "my_filter",
    .type         = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name     = my_get_name,
    .create       = my_create,
    .destroy      = my_destroy,
    .video_render = my_video_render,
    .get_properties = my_get_properties,
};
```

ABI versioning is enforced by `obs_module_ver()` checked against `LIBOBS_API_VER` ([DeepWiki: Plugin Architecture and Loading](https://deepwiki.com/obsproject/obs-studio/4-plugin-system) — "the upper 16 bits of the plugin's `obs_module_ver()` are checked against `LIBOBS_API_VER` and plugins compiled against a newer libobs are rejected"). cpipe should mirror exactly this: encode `(major, minor)` in the entry function and refuse to load mismatches.

What OBS does *not* do well: properties are described imperatively (`obs_properties_create()`, `obs_properties_add_int_slider()`...). For a node graph editor that lives on a static GitHub Pages, we must be able to read the schema *without* running the plugin code. So cpipe replaces OBS's imperative `get_properties` with a JSON manifest at file scope.

### 3.4 GStreamer — capabilities negotiation as inspiration, not literal model

GStreamer's `GstElement` and `GstPad` ([docs](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html), accessed 2026-05-08) implement bidirectional caps negotiation: each pad publishes a `GstCaps` (a list of accepted media types with constraints), and link-time the framework computes the intersection. The lesson for cpipe: per-pad **type capabilities** are first-class, not encoded in the node's name. A `denoise` node that accepts both single-frame and burst inputs declares two pads; the editor draws them; the scheduler fails fast if the user wires incompatibilities.

For cpipe v1 with static topology (D6), full negotiation is overkill — at load time the engine validates the wired graph against per-port caps and emits errors. No runtime renegotiation. But the *vocabulary* (caps, constraints, intersect) is reused. cpipe caps look like:

```json
{ "channels": ["bayer-rggb", "bayer-bggr", "bayer-quad-bayer"],
  "precision": ["fp16", "fp32"], "min_dim": [16,16] }
```

### 3.5 FFmpeg / libavfilter — minimal contract

`AVFilter`'s [`query_formats`](https://ffmpeg.org/doxygen/trunk/structAVFilter.html) callback ([source on GitHub](https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/avfilter.h), accessed 2026-05-08) lets a filter declare which `AVPixelFormat`s it supports for its inputs and outputs. The framework then computes a global format choice that satisfies all filters. cpipe doesn't have FFmpeg's color-format zoo (everything is RGB/Bayer + linear), but the *callback returns a list, framework picks one* idiom is right.

Where FFmpeg is *less* relevant: AVFilter is C-only with no C++ SDK, manual reference counting on `AVFrame`, and no manifest — the schema is read from the source. We pick up the C ABI shape but not the ergonomic gaps.

### 3.6 Halide generator pattern — different paradigm, complementary

Halide's [`HALIDE_REGISTER_GENERATOR`](https://github.com/halide/Halide/blob/main/tutorial/lesson_10_aot_compilation_generate.cpp) (Halide 18.0, accessed 2026-05-08) is *compile-time* registration: a generator declares `Inputs<>` and `Outputs<>`, separates `generate()` from `schedule()`, and the build system invokes it AOT to emit a `.a` per (generator, target-triple) pair. This is exactly how cpipe wants compute kernels to ship — the *plugin* side of cpipe (the C++ SDK), at build time, runs Halide generators to produce CPU-AVX2, CPU-NEON, Vulkan-compute, and Metal kernels, then registers them at runtime through the host's compute suite.

A node implementation in C++ then looks like:
```cpp
class Demosaic : public cpipe::Node {
  void process(cpipe::ComputeContext& cc, ...) override {
    cc.submit_halide(demosaic_amaze_aot, /*inputs*/, /*outputs*/);
  }
};
```
The plugin doesn't pick a backend; the `ComputeContext` (from [#01](01-compute-frameworks.md)) selects the AOT object that matches the device.

### 3.7 Blender add-ons — Python + bpy is not our shape

[Blender's Python add-on model](https://docs.blender.org/api/current/bpy.types.Operator.html) (4.4 LTS, accessed 2026-05-08) leans on `bpy.types.Operator`, `register_class`, and dynamic UI. Powerful for scripting but not relevant for native C++ plugins. The lesson we keep: Blender's *node tree* concept (Compositor, Geometry Nodes) — nodes have typed sockets, the link graph is the data-flow program, evaluation is lazy — is what every modern visual programming environment has converged on. cpipe's DAG model is the same shape.

### 3.8 VST3 — parameter introspection at metadata level

VST3 ([Steinberg dev portal](https://steinbergmedia.github.io/vst3_dev_portal/), accessed 2026-05-08) ships a [`moduleinfo.json`](https://deepwiki.com/juce-framework/JUCE/4.1-audio-plugin-system) describing factory/class info so DAWs can scan plugins without loading the binary. JUCE explicitly uses this for fast scanning. cpipe should ship a `manifest.json` adjacent to (or embedded in) every node that the editor can scrape for parameter UI generation, even before a plugin is ever loaded. This single decision is what lets the GitHub Pages editor display the full node palette of a v2 plugin set without ever running the binary.

VST3 also separates `IAudioProcessor` (real-time) from `IEditController` (UI/parameters), exposed by the host on different threads, with a Cached/Queue between them ("Parameter updates from the audio thread are queued in CachedParamValues and dispatched to the VST3 controller on the message thread at 60Hz to avoid thread-safety issues" — [JUCE wiki](https://deepwiki.com/juce-framework/JUCE/4.1-audio-plugin-system)). cpipe's analogue: the parameter store is a versioned, lock-free copy-on-write structure (see [#03](03-heterogeneous-scheduler.md)); the node's `process()` receives an immutable snapshot.

### 3.9 DaVinci Resolve DCTL — the limit case

DCTL ([Nx Color guide](https://nxcolor.com/understanding-dctl-in-davinci-resolve-the-complete-guide/), 2025) is essentially a per-pixel CUDA-like kernel with `DEFINE_UI_PARAMS` macros. It's tied to color tools and to Resolve Studio (proprietary). cpipe should not import this — but it confirms a pattern: tools where users author shaders directly *do* exist, and the manifest convention for parameters generalizes well. For cpipe v2 we can imagine a "shader node" type that ships a slang shader plus a manifest; this is exactly the same node ABI with a different `process()` body.

### 3.10 Lightroom plugins — historical only

Lightroom's Lua SDK ([Adobe SDK](https://www.adobe.com/devnet/photoshoplightroom.html)) is the legacy alternative to a native plugin. Out of scope for cpipe — Lua is not on the table per D11 (license-friendly, but adds an interpreter and slow GPU bridge). Mentioned for completeness.

### 3.11 ABI versioning summary

Across OFX, OBS, VST3, GStreamer, the same pattern appears:

| Mechanism | OFX | OBS | VST3 | cpipe (proposed) |
|-----------|-----|-----|------|-------------------|
| Plugin entry function | `OfxGetPlugin(int nth)` returns descriptor | `obs_module_load()` calls registration | `GetPluginFactory()` | `cpipe_get_plugin(uint32_t version)` |
| Host version check | Suite negotiation | `LIBOBS_API_VER` upper 16 bits | `kVstFactoryFlagsXXX` | Major/minor compatibility check; minor is additive |
| Backwards strategy | New suites; old plugins ignore | Reject newer ABI | Class IID change | Suite-vtable; reserve trailing slots; never break v1 |

cpipe v1 declares ABI version 1.0; v1.1 adds suites; v2.0 is breakable.

## 4. Architecture sketches for cpipe

### 4.1 The C ABI header (cpipe_node.h)

```c
/* cpipe_node.h — stable C ABI for nodes. C99-compatible. No malloc/free crossing
 * the boundary: host owns all memory, plugin uses opaque handles. */
#ifndef CPIPE_NODE_H
#define CPIPE_NODE_H
#include <stddef.h>
#include <stdint.h>

#define CPIPE_ABI_MAJOR  1
#define CPIPE_ABI_MINOR  0   /* additive; bump when adding suites */

typedef struct cpipe_host_s    cpipe_host_t;     /* opaque, host-owned */
typedef struct cpipe_node_s    cpipe_node_t;     /* opaque, host-owned */
typedef struct cpipe_props_s   cpipe_props_t;    /* opaque, host-owned */
typedef struct cpipe_buffer_s  cpipe_buffer_t;   /* opaque; cross-link to Buffer in #02 */
typedef struct cpipe_compute_s cpipe_compute_t;  /* opaque; cross-link to ComputeContext in #01 */

/* Status codes — keep numeric values stable forever */
enum {
    CPIPE_OK             = 0,
    CPIPE_FAILED         = 1,
    CPIPE_REPLY_DEFAULT  = 2,   /* OFX: kOfxStatReplyDefault — host falls back */
    CPIPE_OOM            = 3,
    CPIPE_BAD_PRECISION  = 4,
    CPIPE_BAD_INDEX      = 5,
    CPIPE_NEED_PARAM     = 6,
    CPIPE_INTERNAL_ERROR = 7,
};

/* Action strings — additive over time, plugin returns CPIPE_REPLY_DEFAULT for unknown */
#define CPIPE_ACTION_DESCRIBE      "describe"        /* manifest scrape; no compute */
#define CPIPE_ACTION_CREATE        "create"          /* per-instance state */
#define CPIPE_ACTION_DESTROY       "destroy"
#define CPIPE_ACTION_PREPARE       "prepare"         /* called once after wiring; can JIT */
#define CPIPE_ACTION_PROCESS       "process"         /* execute node */
#define CPIPE_ACTION_PARAM_CHANGED "param_changed"   /* user moved a slider */

/* Buffer suite — the plugin's only door to image data. See #02. */
typedef struct {
    int (*get_dims)(const cpipe_buffer_t*, size_t* w, size_t* h, size_t* c);
    int (*get_precision)(const cpipe_buffer_t*, int* /* CPIPE_PREC_* */);
    int (*map_read)(const cpipe_buffer_t*, void** ptr, size_t* row_stride);
    int (*unmap)(const cpipe_buffer_t*);
    int (*colorspace_id)(const cpipe_buffer_t*, const char** ocio_role);
} cpipe_buffer_suite_v1;

/* Compute suite — submit Halide AOT objects, slang shaders, or inference graphs.
 * See #01 (frameworks) and #03 (scheduler). */
typedef struct {
    int (*submit_halide_aot)(cpipe_compute_t*, const char* aot_id,
                             const cpipe_buffer_t* const* inputs, size_t n_in,
                             cpipe_buffer_t* const* outputs, size_t n_out);
    int (*submit_slang)(cpipe_compute_t*, const char* slang_module_id,
                        const char* entry_point,
                        const cpipe_buffer_t* const* inputs, size_t n_in,
                        cpipe_buffer_t* const* outputs, size_t n_out,
                        const void* push_constants, size_t pc_size);
    int (*submit_inference)(cpipe_compute_t*, const char* model_id,
                            const cpipe_buffer_t* const* inputs, size_t n_in,
                            cpipe_buffer_t* const* outputs, size_t n_out);
    int (*record_marker)(cpipe_compute_t*, const char* label);  /* profiling */
} cpipe_compute_suite_v1;

/* Param suite — typed access to the parameter store */
typedef struct {
    int (*get_double)(const cpipe_props_t*, const char* key, double* out);
    int (*get_int)(const cpipe_props_t*, const char* key, int64_t* out);
    int (*get_enum)(const cpipe_props_t*, const char* key, const char** out);
    int (*get_curve)(const cpipe_props_t*, const char* key,
                     const float** xs, const float** ys, size_t* n);
    int (*get_lut3d)(const cpipe_props_t*, const char* key,
                     const float** rgb, size_t* edge);
    int (*get_color)(const cpipe_props_t*, const char* key, float rgba[4]);
} cpipe_param_suite_v1;

/* Host — gives the plugin access to the suites */
struct cpipe_host_s {
    uint32_t abi_major; uint32_t abi_minor;
    const void* (*get_suite)(cpipe_host_t* self, const char* suite_name, int version);
    void  (*log)(cpipe_host_t* self, int level, const char* msg);
    /* Allocator for plugin-side metadata; do not pass image buffers across this. */
    void* (*alloc)(cpipe_host_t*, size_t);
    void  (*free)(cpipe_host_t*, void*);
};

/* The plugin's main entry. One per node type. Host calls this for every action. */
typedef int (*cpipe_main_entry_t)(const char* action,
                                   cpipe_host_t* host,
                                   cpipe_node_t* node,
                                   cpipe_props_t* params,
                                   /* per-action context — see action docs */
                                   void* in_ctx, void* out_ctx);

/* The plugin descriptor — what the host registers */
typedef struct {
    uint32_t abi_major;        /* must equal CPIPE_ABI_MAJOR */
    uint32_t abi_minor;        /* must <= host's CPIPE_ABI_MINOR */
    const char* node_id;       /* "com.cpipe.demosaic.amaze" */
    const char* manifest_json; /* embedded; see §4.3 */
    cpipe_main_entry_t main_entry;
} cpipe_plugin_desc_t;

/* The single C symbol every plugin .so exports.
 * Built-in nodes link this directly. v2 dlopens it. */
typedef int (*cpipe_register_fn_t)(cpipe_host_t* host,
                                   void (*register_one)(cpipe_host_t*,
                                                        const cpipe_plugin_desc_t*));
#endif /* CPIPE_NODE_H */
```

This header is the complete plugin contract for v1. Ten suites' worth of headers can be added in v1.1 without touching v1.0 callers. Notice:

- No C++ types crossing the boundary; no `std::string`, no STL.
- All allocations are host-owned; the plugin only borrows pointers.
- Adding a new action is a no-op for old plugins (they return `CPIPE_REPLY_DEFAULT`).
- The `manifest_json` string is embedded in the descriptor — the host can introspect without invoking any plugin code, satisfying the VST3 fast-scan requirement (§3.8).

### 4.2 The C++ SDK

Plugins compile against `cpipe_node.h` directly, but ergonomically they want a C++ wrapper. Same pattern as OFX's `Support/include/ofxsImageEffect.h`. The C++ SDK is single-header, lives in cpipe's repo, and is BSD-3 (matches OFX) so v2 third-parties can link it without infecting their own license.

```cpp
// cpipe/sdk.hpp — C++ SDK, header-only. Built-in nodes use this; v2 plugins use this.
namespace cpipe::sdk {

class ComputeContext {
public:
    void submit_halide(std::string_view aot_id,
                       std::span<const Buffer*> inputs,
                       std::span<Buffer*> outputs);
    void submit_slang(std::string_view module, std::string_view entry,
                      std::span<const Buffer*> inputs,
                      std::span<Buffer*> outputs,
                      const void* push_constants = nullptr,
                      size_t pc_size = 0);
    void submit_inference(std::string_view model_id,
                          std::span<const Buffer*> inputs,
                          std::span<Buffer*> outputs);
    void mark(std::string_view label) noexcept;
private:
    cpipe_compute_t* impl_;
    const cpipe_compute_suite_v1* suite_;
};

class Buffer {  // wraps cpipe_buffer_t. See report #02.
public:
    size_t width() const; size_t height() const; size_t channels() const;
    Precision precision() const;   // enum: FP16, FP32, U8, U16
    std::string_view colorspace() const; // OCIO role
    // No raw pointer — backend-managed; Halide/slang use it directly.
};

class ParamView {                  // wraps cpipe_props_t (read-only snapshot)
public:
    double                  d(std::string_view key) const;
    int64_t                 i(std::string_view key) const;
    std::string_view        s(std::string_view key) const;
    std::span<const float>  curve(std::string_view key) const;  // x[],y[]
};

// Plugin authors derive from this:
class Node {
public:
    virtual ~Node() = default;
    virtual void prepare(ComputeContext&, const ParamView&) {}    // optional
    virtual void process(ComputeContext&, const ParamView&,
                         std::span<const Buffer*> inputs,
                         std::span<Buffer*> outputs) = 0;
    virtual void on_param_changed(std::string_view key, const ParamView&) {}
};

// One macro to register, identical for built-in and external:
#define CPIPE_REGISTER_NODE(klass, manifest_json_literal) \
    extern "C" const cpipe_plugin_desc_t* cpipe_desc_##klass(void) { \
        static const cpipe_plugin_desc_t d = { \
            .abi_major = CPIPE_ABI_MAJOR, .abi_minor = CPIPE_ABI_MINOR, \
            .node_id = klass::ID, .manifest_json = manifest_json_literal, \
            .main_entry = &cpipe::sdk::detail::dispatch<klass> \
        }; return &d; }

}  // namespace cpipe::sdk
```

A node implementation:

```cpp
// nodes/demosaic_amaze.cpp
#include "cpipe/sdk.hpp"
#include "demosaic_amaze.h"   // Halide AOT header
namespace cpipe::nodes {

class DemosaicAMAZE : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.demosaic.amaze";
    void process(sdk::ComputeContext& cc, const sdk::ParamView& p,
                 std::span<const sdk::Buffer*> in, std::span<sdk::Buffer*> out) override {
        cc.submit_halide("demosaic_amaze", in, out);
    }
};

}  // namespace
CPIPE_REGISTER_NODE(cpipe::nodes::DemosaicAMAZE,
R"({"id":"com.cpipe.demosaic.amaze","ports":[
  {"name":"in",  "kind":"in",  "caps":{"channels":["bayer-rggb","bayer-bggr","bayer-quad-bayer"],"precision":["u16","fp16"]}},
  {"name":"out", "kind":"out", "caps":{"channels":["rgb"],"precision":["fp16"]}}],
"params":[]})");
```

The macro emits a `cpipe_plugin_desc_t`. A built-in node's `cpipe_desc_*` symbol is collected at link time into a registry table; an external plugin's `.so` exports it. **Same code, different build target** — D4 satisfied without runtime cost.

### 4.3 Manifest schema (JSON Schema 2020-12)

A node's manifest describes everything the host needs to wire it, validate parameters, and render a UI in the editor:

```json
{
  "$schema": "https://schemas.cpipe.dev/node/v1.json",
  "id": "com.cpipe.denoise.bm3d",
  "version": "1.0.0",
  "category": "denoise/single-frame",
  "label": "BM3D Denoise",
  "doc": "Block-matching 3D denoise. CPU only in v1.",
  "ports": [
    { "name":"in",  "kind":"in",
      "caps": { "channels":["rgb","linear-rgb"], "precision":["fp16","fp32"] } },
    { "name":"out", "kind":"out",
      "caps": { "channels":["rgb"], "precision":["fp16"] } }
  ],
  "params": [
    { "name":"sigma", "type":"float", "min":0.0, "max":50.0, "step":0.1, "default":3.0,
      "label":"Noise std.dev", "ocio_invariant":true },
    { "name":"profile", "type":"enum",
      "choices":["high-precision","balanced","fast"], "default":"balanced",
      "depends_on": [{ "param":"sigma", "predicate":"sigma > 0" }] },
    { "name":"luma_only", "type":"bool", "default":false }
  ],
  "compute": {
    "backends_required": ["cpu"],
    "backends_optional": ["vulkan"],
    "halide_aot": ["bm3d_cpu_avx2","bm3d_cpu_neon"],
    "memory_bound_mb_per_mp": 12
  },
  "color": {
    "input_role": "scene_linear",
    "output_role": "scene_linear",
    "respects_chromaticity": true
  },
  "test": {
    "golden_inputs":  ["test/golden/bm3d/in_*.exr"],
    "golden_outputs": ["test/golden/bm3d/out_*.exr"],
    "tolerance_psnr_db": 50.0,
    "rng_seed": 42
  }
}
```

Validated against a JSON Schema using [Ajv](https://ajv.js.org/) ([8.x, accessed 2026-05-08, 50M weekly npm downloads](https://github.com/ajv-validator/ajv)) on the editor side, and [nlohmann/json](https://github.com/nlohmann/json) + a C++ schema validator on the host. The manifest is the single source of truth for: editor UI (parameter widgets), graph validator (port caps, precision), scheduler (precision conversions per D9), test harness (golden images), color manager (OCIO roles per [#13](13-color-management.md)).

### 4.4 Internal nodes are plugins (D4 vision honored)

The user's stated philosophy is that internal ISP nodes are *also* plugins. This is the right call architecturally. Here is how it works without runtime cost:

1. Each internal node lives in its own translation unit and ends with `CPIPE_REGISTER_NODE(...)`.
2. The build system uses [linker section attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html) (`__attribute__((section("cpipe_registry")))` on Linux/macOS, `#pragma section` on MSVC) to collect every `cpipe_plugin_desc_t*` into a contiguous array at link time.
3. The host's startup function walks `__start_cpipe_registry` to `__stop_cpipe_registry`, calling each descriptor's `main_entry` with `CPIPE_ACTION_DESCRIBE`. Total cost at startup: O(number of nodes), each call returns immediately for built-ins.
4. The runtime registry is `std::unordered_map<std::string_view, const cpipe_plugin_desc_t*>` populated from this walk; lookups are O(1).

**No interpreter, no `dlopen`, no JSON parsing at every node call.** Manifest JSON is parsed once at startup, cached. A built-in node's `cpipe_main_entry_t` for `CPIPE_ACTION_PROCESS` does a trivial dispatch to the C++ class via `static_cast` — measured cost on the order of a virtual call, around a nanosecond.

This pattern is used by the Linux kernel (`__initcall`), by Catch2 (test registration), by [protocol buffers](https://protobuf.dev/) (descriptor pool). Battle-tested.

### 4.5 Per-node manifest precision (D9)

Each port's `caps.precision` enumerates accepted formats, in *order of preference*. The scheduler ([#03](03-heterogeneous-scheduler.md)) walks the wired graph from inputs forward, picks the *first preference of the producer* unless the consumer's preference list excludes it; if so it inserts a conversion node. Because the manifest is static and the topology is locked (D6), this resolution is computed once at graph load. Resulting graph stores explicit conversion nodes (no implicit conversions during process), which makes profiling honest.

Example: a Bayer demosaic outputs `["rgb", "fp16"]`; downstream BM3D accepts `["rgb"], ["fp16","fp32"]`. No conversion needed. A WB node outputs `["rgb","fp32"]`; if its downstream tone-curve only accepts FP16 (memory-bound), the scheduler inserts a `convert_fp32_to_fp16` node. The `convert` node is itself a plugin — registered through the same machinery, with a Halide-AOT body.

### 4.6 Plugin-side compute artifacts

A node author has three options for kernels:

1. **Halide AOT.** Author writes a generator (`.cpp`), build pipeline runs Halide's `make_module` at build time emitting `.a` per `(target, schedule)`. The plugin links these; at runtime calls `cc.submit_halide("name", inputs, outputs)`. The compute backend dispatches to the matching variant (CPU-AVX2, CPU-NEON, Vulkan, Metal). Cross-link to [#01 §Halide](01-compute-frameworks.md).

2. **Slang shader.** Author writes `.slang`; build pipeline runs `slangc` once to a SPIR-V/MSL/DXIL bundle, embedded in the plugin as a single binary blob. Runtime calls `cc.submit_slang("module", "main", ...)`. Slang-rhi (per [#01](01-compute-frameworks.md)) handles cross-API dispatch.

3. **Quantized inference graph.** A `.pte` (ExecuteTorch) or `.onnx` is shipped as data; runtime calls `cc.submit_inference("model.pte", ...)`. Cross-link to [#04](04-mobile-ai-inference.md).

In all three cases, the host owns the device queue, the memory allocator, and the synchronization primitives ([#02 timeline semaphores](02-zero-copy-buffer-architecture.md)). The plugin is a pure consumer of compute.

### 4.7 Test harness — golden images per node

Every node ships a `test/` directory with input EXRs (or DNGs for raw-only nodes), golden outputs, and an entry in the manifest declaring tolerance. The CI pipeline (per [#09](09-image-quality-benchmarks.md) IQA primitives) runs:

```cpp
for (auto& node : registry) {
  for (auto& [in, gold] : node.manifest().test_pairs()) {
    Buffer out = run_node(node, in);
    auto psnr = full_ref_psnr(gold, out);
    auto ssim = full_ref_ssim(gold, out);
    REQUIRE(psnr >= node.manifest().tolerance_psnr());
    REQUIRE(ssim >= 0.99);
  }
}
```

Golden images are version-pinned by content hash; a node update either keeps the existing tolerance or bumps the manifest version (semver minor). RNG seed is in the manifest so denoising tests are deterministic. Cross-link to [#09](09-image-quality-benchmarks.md) for the IQA implementation choice (we recommend [IQA-PyTorch](https://github.com/chaofengc/IQA-PyTorch) for offline evaluation, with an embedded subset for CI).

For burst-fusion nodes (per D3), tests ship a 5–10 frame sequence with deterministic alignment. For raw-input nodes, golden DNGs are kept tiny (256×256 crop) to keep CI fast.

### 4.8 Sandboxing reservation (D4)

v1 does not sandbox — there are no third-party plugins to sandbox. But the architecture keeps the door open:

- The plugin only sees opaque handles; the host can switch to "shadow" implementations (a remote-process bridge) without changing plugin code.
- All memory crossing the boundary is allocator-mediated through the host (the plugin never `malloc`s an image buffer).
- All file I/O is through the host (a future `cpipe_io_suite_v1` returns opaque `cpipe_file_t*`).
- Future v2 implementation can spawn each plugin in a subprocess with `seccomp` (Linux) / `App Sandbox` (macOS) / Android `isolatedProcess`, using a [Cap'n Proto](https://capnproto.org/) bridge over a shared-memory ring. The C ABI does not change.

### 4.9 Hot reload

Out of v1 scope but the architecture supports it:

- Built-in nodes obviously cannot hot-reload.
- v2 third-party `.so` reload uses the OFX trick: every node instance holds a generation counter; on reload the host instantiates a new descriptor, marks old instances dirty, drains the scheduler queue, swaps the descriptor pointer, then re-creates instances from the saved param state. The graph topology is replayed from the JSON. Total downtime: tens of ms.

### 4.10 Comparison summary

| Aspect | OFX | OBS | VST3 | GStreamer | ComfyUI | **cpipe** |
|--------|-----|-----|------|-----------|---------|-----------|
| ABI | C, header-only | C struct of fn-ptrs | C++ COM-like | C with GObject | Python | **C, header-only** |
| Manifest | Suite-introspected | Imperative | `moduleinfo.json` | `.so` introspection at load | Classmethod | **JSON adjacent to binary** |
| Param introspection w/o load | Partial | No | Yes (via `moduleinfo.json`) | No | N/A | **Yes** |
| Compute access | Host-mediated | OpenGL effect API | Audio buffer | Memory mapping | Python | **`ComputeContext` host-mediated** |
| GPU access | Optional | Optional | None (audio) | Various | Via Python | **Halide / slang / inference, host-dispatched** |
| Built-in registration | Static factory | Static struct | Static factory | `gst_element_register` | Module import | **Linker section + `CPIPE_REGISTER_NODE`** |
| Sandboxing | No (host responsibility) | No | No | No | No | **Reserved (subprocess + seccomp planned for v2)** |
| Hot reload | No standardized | Per-source supported | Yes | `gst-bin-replace` | Restart needed | **v2; static v1** |
| License | BSD-3 | GPLv2 | GPLv3/Steinberg dual | LGPL | GPLv3 | **Apache 2.0** |

## 5. Open questions

1. **Does the C ABI need a Pascal-string convention for stable layouts?** OFX uses null-terminated C strings everywhere; we follow. But VST3 has discovered subtle issues with locale-dependent comparisons. Keep all string keys ASCII-only and document.
2. **Should manifests be embedded *and* shipped as separate files?** Embedding (in the descriptor) lets the host introspect a binary; separate files let the editor scan a directory of unrun plugins. Both are cheap; we ship both. Open: should the separate file be authoritative or the embedded one?
3. **How do we handle plugin versioning when two plugins claim the same `node_id`?** Reject; prefer the higher semver; warn. Logic is in the registry, not the plugin. Will a "channel" namespace be needed (stable / beta / dev)?
4. **Should `Node::process` be allowed to recursively call into the host's scheduler?** Today no — process is a leaf. But some nodes (e.g. tile-based out-of-core that we deferred per D2) want to subdivide. Keep `process` as a leaf for v1, design a `Pipeline::sub_pipeline()` API for v2.
5. **How does a plugin discover OCIO color-space roles when the host's OCIO config is dynamic?** The manifest says "scene_linear"; the host resolves to the active OCIO config's role. If the role is missing, plugin returns `CPIPE_FAILED` with a structured error. Cross-link to [#13](13-color-management.md) — that report should pin OCIO 2.x role names.
6. **Plugin-author CMake template** — open whether we ship one. OFX does, OBS does. We should, to make the v2 transition trivial.
7. **Plugin signing?** Out of v1 scope per D4. v2 should sign manifests with Ed25519 and ship a public-key-pinned registry. Reserve a `manifest.sig` file format now.

## 6. Cited sources

- ComfyUI custom node walkthrough — `https://docs.comfy.org/custom-nodes/walkthrough` (accessed 2026-05-08).
- ComfyUI V3 schema migration — `https://apatero.com/blog/comfyui-v3-custom-node-schema-development-2026` (accessed 2026-05-08).
- ComfyUI Nodes 2.0 announcement — `https://blog.comfy.org/p/comfyui-node-2-0` (accessed 2026-05-08).
- ComfyUI-Manager — `https://github.com/Comfy-Org/ComfyUI-Manager` (accessed 2026-05-08).
- ComfyUI dependency resolution — `https://comfyui.org/en/comfyui-v3-dependency-resolution` (accessed 2026-05-08).
- LiteGraph.js (archived) — `https://github.com/Comfy-Org/litegraph.js` (accessed 2026-05-08).
- OpenFX repository, v1.5.1 (Nov 2025) — `https://github.com/AcademySoftwareFoundation/openfx` (accessed 2026-05-08).
- OpenFX Programming Guide — `https://openfx.readthedocs.io/en/latest/Guide/index.html` (accessed 2026-05-08).
- OpenFX Image Effect API — `https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectAPI.html` (accessed 2026-05-08).
- OpenFX Effect Parameters — `https://openfx.readthedocs.io/en/main/Reference/ofxParameter.html` (accessed 2026-05-08).
- OBS Plugin docs (32.1.2) — `https://docs.obsproject.com/plugins` (accessed 2026-05-08).
- OBS Source API reference — `https://docs.obsproject.com/reference-sources` (accessed 2026-05-08).
- OBS Plugin System architecture — `https://deepwiki.com/obsproject/obs-studio/4-plugin-system` (accessed 2026-05-08).
- GStreamer GstElement docs — `https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html` (accessed 2026-05-08).
- GStreamer GstPad docs — `https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html` (accessed 2026-05-08).
- GStreamer caps negotiation — `https://gstreamer.freedesktop.org/documentation/additional/design/negotiation.html` (accessed 2026-05-08).
- FFmpeg libavfilter avfilter.h — `https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/avfilter.h` (accessed 2026-05-08).
- FFmpeg filters documentation — `https://ffmpeg.org/ffmpeg-filters.html` (accessed 2026-05-08).
- Halide AOT generators (lesson 10) — `https://github.com/halide/Halide/blob/main/tutorial/lesson_10_aot_compilation_generate.cpp` (accessed 2026-05-08).
- Halide CMake integration — `https://halide-lang.org/docs/md_doc_2_halide_c_make_package.html` (accessed 2026-05-08).
- VST3 dev portal — `https://steinbergmedia.github.io/vst3_dev_portal/` (accessed 2026-05-08).
- VST3 module info JSON — `https://deepwiki.com/juce-framework/JUCE/4.1-audio-plugin-system` (accessed 2026-05-08).
- DaVinci Resolve DCTL guide — `https://nxcolor.com/understanding-dctl-in-davinci-resolve-the-complete-guide/` (accessed 2026-05-08).
- Blender Operator API (4.x) — `https://docs.blender.org/api/current/bpy.types.Operator.html` (accessed 2026-05-08).
- Apache compatibility (BSD-3, MIT, Apache, etc.) — `https://www.apache.org/legal/resolved.html` (accessed 2026-05-08).
- Ajv JSON Schema validator — `https://github.com/ajv-validator/ajv` (accessed 2026-05-08).
- nlohmann/json — `https://github.com/nlohmann/json` (accessed 2026-05-08).

## 7. See also

- [#01 — Compute Frameworks](01-compute-frameworks.md): `ComputeContext`, slang-rhi, Halide AOT runtime.
- [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md): `cpipe::Buffer`, AHardwareBuffer/IOSurface/Vulkan-external mappings.
- [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md): TaskFlow-based DAG executor; per-node profiler that feeds the editor.
- [#04 — Mobile AI Inference](04-mobile-ai-inference.md): ExecuteTorch / ONNX Runtime / MNN selection — referenced by `submit_inference`.
- [#05 — NPU Backends and Zero-Copy](05-npu-backends-zero-copy.md): Hexagon / ANE; same `submit_inference` entry point.
- [#06 — Soft ISP Architectures](06-soft-isp-architectures.md): vkdt's modular nodes; darktable's `iop` ABI lessons.
- [#07 — Classic ISP Algorithms](07-classic-isp-algorithms.md): demosaic / WB / tone — concrete plugin nodes.
- [#08 — AI ISP Algorithms](08-ai-isp-algorithms.md): NAFNet / Restormer / Burst Photography — AI plugin nodes.
- [#09 — Image Quality Benchmarks](09-image-quality-benchmarks.md): test harness, IQA-PyTorch.
- [#11 — Pipeline Editor & Connectivity](11-pipeline-editor-and-connectivity.md): the editor consumes manifests; protocol carries manifest deltas.
- [#13 — Color Management](13-color-management.md): OCIO roles referenced in manifest `color.input_role`.
- [#14 — HEIF and HDR Output](14-heif-and-hdr-output.md): output node implements UltraHDR encode through this same plugin ABI.
