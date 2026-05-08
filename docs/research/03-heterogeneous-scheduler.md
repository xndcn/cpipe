# 03 — Heterogeneous Scheduler

> Cluster A · Compute Foundation · Report 3 of 3
> Sibling reports: [#01 — Compute Frameworks](01-compute-frameworks.md) · [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md)
> Date: 2026-05-08

---

## 1. TL;DR

cpipe's DAG scheduler runs heterogeneously across CPU + Vulkan/Metal GPU + Hexagon DSP/NPU + Apple Neural Engine, with **static topology after load** [D6] and **batch-only** execution [D5]. The recommended scheduler is a thin **device-plane** layer above **TaskFlow v4.0.0 (released 2026-01-02, MIT-licensed, header-only)**, which wins on programmability (decentralized work-stealing, conditional tasking, pipeline parallelism, integrated profiler `TFProf`) and license fit. Halide and Slang both contribute *kernel* execution; TaskFlow contributes *graph* orchestration. The scheduler treats every node as `(input buffers, kernel, output buffer, device, fence values)` and compiles the static cpipe DAG once at load to produce: (a) a memory plan (peak GPU footprint, allocation arena per device, ≤ 6 in-flight FP16 intermediates by default for 100 MP @ FP16), (b) a per-edge precision plan that inserts the minimum number of conversions [D9], (c) a per-edge device assignment with explicit `Handoff` markers wherever an edge crosses devices. Synchronization uses Vulkan timeline semaphores and Metal `MTLSharedEvent` (see [#02 §5]). Profiling integrates Perfetto traces (`render-stage` data sources for Vulkan), Tracy (CPU + GPU markers), and a Chrome-Trace-JSON export for the editor. The static-topology + batch-only constraints simplify dramatically — no preemption, no streaming pipeline; but the architecture is shaped so a v2 streaming variant only needs to add token semantics on edges. Pseudocode for graph compilation, memory planning, scheduling loop, manifest schema, and cross-device hand-off is in §11.

---

## 2. Decision Matrix — orchestration runtime

| # | Runtime | Latest tag (verified 2026-05-08) | License | Header-only / build burden | Heterogeneous (GPU/NPU) coordination | Conditional / pipeline | Profiler | Score for cpipe |
|---|---------|-------------------------------------|---------|--------------------------------|---------------------------------------|----------------------------|----------|---|
| 1 | **TaskFlow** | `v4.0.0` 2026-01-02; HEAD `93c307d160` 2026-05-03 | MIT | Header-only, CMake/FetchContent, no deps | `cudaFlow` / `syclFlow` for GPU; `Pipeline`; async tasks | Yes — `condition_task`, `Pipeline`, `Subflow` | `TFProf` (web) | **Pick** |
| 2 | oneTBB Flow Graph | `v2023.0.0` 2026-04-30 | Apache 2.0 | UXL Foundation (was Intel oneAPI); CMake; sizable | Flow Graph nodes; SYCL flows; not directly GPU | Yes — gateway/buffer/multifunction | VTune/Inspector (commercial) | Reject — heavier infra, weaker mobile reach |
| 3 | HPX | `v1.11.0` 2025-06-29 | Boost 1.0 | CMake; depends on hwloc, Boost; large | Distributed-aware (overkill for cpipe) | Yes via continuations | APEX | Reject — not built for headless mobile + huge runtime |
| 4 | Folly Futures + Executors | rolling; `main` HEAD daily | Apache 2.0 | CMake; depends on Boost, glog, double-conversion, etc. | Manual (you wire device queues) | Yes — `then`, `via` | manual | Reject — no native graph model; we'd build ours |
| 5 | Galois | rolling main; sparse releases | BSD-3-Clause | CMake; Boost dep | Graph-iteration semantics (irregular parallelism) | Yes — for-each w/ condition | manual | Reject — wrong shape (irregular graph mining vs. fixed image DAG) |
| 6 | libdispatch (GCD) | swift-corelibs-libdispatch HEAD | Apache 2.0 | small | No graph model | Limited — dispatch_group | Instruments (Apple) | Reject — not a DAG; useful as Apple-only thread pool below TaskFlow |
| 7 | Halide internal scheduler | bundled with Halide v21.0.0 | MIT | with Halide | Halide-internal only | No (intra-`Func`) | Halide profiler | Adopt as **kernel-internal** parallelism; not the cpipe scheduler |
| 8 | vkdt scheduler | vkdt main | mostly BSD-2 with some GPLv3 | with vkdt | Custom Vulkan | n/a | none | Read for ideas only — D11 license-tainted [#06] |
| 9 | Blender compositor / OBS GraphicsThread / GStreamer task-pool | various | various | various | various | n/a | various | Studied for ideas; none directly reusable |

The recommendation is unambiguous: **TaskFlow v4.0.0**, MIT, header-only via FetchContent, with a thin cpipe-owned device plane on top.

---

## 3. Why TaskFlow v4.0.0

The user's locked-decisions list called out TaskFlow as a "starting candidate" and pointed at a GitHub URL `taskflow/v4.0.0`. Verification: TaskFlow `v4.0.0` was tagged on 2026-01-02 (per `gh api repos/taskflow/taskflow/releases`); HEAD is `93c307d160` on 2026-05-03 (active maintenance — daily commits in 2026). License: MIT.

**Programming model shape — concrete features that map onto cpipe.**

- **Task (`tf::Task`).** A single `std::function<void()>` body in a `tf::Taskflow`. Dependencies via `precede(...)` / `succeed(...)`. cpipe's nodes (one per ISP step) become tasks.
- **`tf::Executor`.** The work-stealing thread pool that runs taskflows. Decentralized (per-thread queues with random victim selection) — measured as faster than centralized queues on 16+ core hosts. cpipe will hold one global Executor.
- **`tf::Pipeline`.** Pipeline parallelism for staged data tokens; not used in v1 (D5: batch-only) but reserved for v2 streaming.
- **`tf::Subflow`.** Nested tasks emitted from inside a parent task — useful when a node body wants to fan out (e.g. multi-frame fusion needs N parallel align kernels).
- **`tf::cudaFlow` / `tf::syclFlow`.** GPU tasking primitives — for cpipe we use these *not* for our Vulkan dispatch (Vulkan has its own command-buffer model; we don't go through CUDA), but we may use `cudaFlow` on Linux desktop NVIDIA when a kernel has a CUDA path (Halide-CUDA backend). On Apple/Android we ignore these.
- **Conditional tasking (`tf::Task::condition`).** A task body returns an `int` index that selects which downstream branch fires. cpipe doesn't use this in v1 (D6: static topology) but the **batch retry/error-recovery path** uses it (e.g. on out-of-memory, fall back to spilling — see §6).
- **Async tasks (`Executor::async`, `Executor::silent_async`).** Out-of-graph immediate execution — useful for the "fire device-side dispatch and continue" pattern.
- **Profiler `TFProf`.** Built-in profiling that exports JSON viewable in a web UI; hooks into Chrome trace viewer. We already need Chrome trace JSON for the editor — convenient.
- **CMake / FetchContent integration.** TaskFlow is header-only; you `FetchContent_Declare(taskflow GIT_REPOSITORY ... GIT_TAG v4.0.0)` then `target_link_libraries(cpipe PRIVATE Taskflow::Taskflow)`. No transitive deps. Perfect for [D18].

**Why not the alternatives?**

- **oneTBB Flow Graph.** Mature and Apache 2.0, but the build is heavier (UXL Foundation governance handed it off from Intel; latest is `v2023.0.0` 2026-04-30). It is a complete runtime (not header-only), which conflicts with cpipe's preference for minimal externals. Flow Graph is also more "message-passing-network" shaped than DAG-shaped — which is fine semantically, but TaskFlow's clearer DAG vocabulary matches cpipe's mental model.
- **HPX.** Excellent for distributed HPC, overkill for a single-process app. The Boost / hwloc dependency tree is unwelcome.
- **Folly.** No native graph model; we'd hand-roll DAG semantics on top of Futures. Folly is also a big monolith (you cannot easily pick out just the futures component).
- **Galois.** Targeted at irregular graph mining (graph algorithms with worklists). cpipe has the opposite problem — a small, regular, *known-at-load-time* image-processing DAG.
- **libdispatch (GCD).** Apple-specific (and the Linux port is partial). Useful as a *thread pool* on Apple but not as the DAG layer.

---

## 4. Architecture overview

```
        ┌────────────────────────────────────────────────┐
        │            cpipe Pipeline (load-time)          │
        │ DAG description (JSON) → Compile → IR          │
        └─────────────────┬──────────────────────────────┘
                          │
                          ▼
        ┌────────────────────────────────────────────────┐
        │          cpipe Scheduler (this report)         │
        │  - graph compile  - memory planner             │
        │  - precision planner  - device assignment      │
        │  - TaskFlow Executor  - profile capture        │
        └────┬───────────┬────────────┬───────────┬──────┘
             ▼           ▼            ▼           ▼
       ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
       │ CPU     │ │ Vulkan  │ │ Metal    │ │ NPU/DSP  │
       │ Halide+ │ │ Halide+ │ │ Halide+  │ │ QNN /    │
       │ slang-  │ │ slang-  │ │ slang-   │ │ Core ML /│
       │ rhi/CPU │ │ rhi/Vk  │ │ rhi/Mtl  │ │ Hexagon  │
       └─────────┘ └─────────┘ └──────────┘ └──────────┘
             ▲           ▲            ▲           ▲
             └───── Unified IBuffer ──┴───────────┘
                       (Report 02)
```

Three layers:

1. **Pipeline IR (cpipe-owned).** A `std::vector<NodeIR>` after compilation; each `NodeIR` knows its input buffer ids, kernel id, output buffer ids, requested device, precision in/out, and a manifest describing memory cost.
2. **Scheduler (this report).** Owns the TaskFlow `tf::Executor`, the per-device command queue list (`std::vector<std::unique_ptr<ICommandQueue>>` from [#01]), the memory arena (one per device), and the timeline semaphore lifecycle. Drives the run loop.
3. **Device backends.** Halide AOT artifacts and Slang-RHI compute pipelines invoked by the scheduler. The buffer fabric is from [#02].

---

## 5. Memory planning

100 MP @ FP16 ≈ 200 MB / intermediate. v1 budget [D2] is 800 MB at FP32 worst case; we work in FP16 and aim for ≤ 6 concurrent intermediates ⇒ ~1.2 GB peak GPU memory.

**Algorithm — per-device memory plan.** Standard liveness analysis on the DAG:

1. Topological-sort the nodes.
2. For each edge `e: u → v` compute its lifetime interval `[birth(u), death(v))`. Death of edge `e` is the topo position of its last consumer.
3. Group edges by device.
4. Per device, run a graph-coloring "interference" pass — two edges interfere iff their intervals overlap. Assign each edge a slot in the device's memory pool. Slot count = peak number of concurrent edges = chromatic number of interference graph.
5. Multiply slot count × max-edge-size = device's memory budget. If > device cap, fail with a clear "graph won't fit" error (v2 will spill).

This is a textbook offline register allocator applied to GPU memory; the DAG is small (typically < 50 nodes for a cpipe pipeline) so the algorithm is exact in microseconds.

**Memory pool / arena strategy.** Per device we pre-allocate one large `VkDeviceMemory` (or `MTLHeap`) sized to the planner's peak, then sub-allocate slot-shaped regions. Sub-allocation is bookkeeping only, no driver call. This avoids per-frame `vkAllocateMemory` / `MTLDevice newBuffer` (slow on every platform tested in 2024–2026 reports) and keeps device-memory fragmentation under our control.

**Edge contract.** An edge is `(slot_id, IBuffer descriptor, format, color_space, precision, layout)`. The slot is just an offset+size into the per-device arena. The IBuffer wraps that view. See [#02 §7].

**Spill (escape hatch, v1 disabled by default).** When the planner cannot fit, the cpipe `--spill` CLI flag enables forced staging of intermediates to system RAM between nodes via `vkCmdCopyBufferToBuffer`. This is **not zero-copy** and is documented as such; v1 default is `--no-spill` (fail loudly).

**Burst memory.** With 5–10 input frames each 200 MB at RAW16 100 MP, burst inputs alone = 1–2 GB. The planner treats inputs as long-lived edges (alive from load to fusion-node) and verifies fit before any compute. For a 100 MP burst, we may auto-reduce to 5 frames; for 50 MP sensors (Apple, Pixel, Galaxy "Pro" mode) 10 frames is fine. This is policy in the manifest, not hardcoded.

---

## 6. Per-node manifest schema (D9: precision policy)

Every cpipe node ships a manifest. Manifests are static — they live in a `node.json` shipped with the node binary (the build artifacts referenced from [#01]). The scheduler reads them at load and uses them for device assignment, precision planning, and memory planning.

```json
{
  "$schema": "cpipe.node.manifest.v1",
  "name":            "demosaic_ahd",
  "version":         "1.0.0",
  "license":         "Apache-2.0",
  "kind":            "kernel",
  "implementations": [
    {
      "device":     "Vulkan",
      "engine":     "Halide",
      "artifact":   "demosaic_ahd-vulkan.spv",
      "in_format":  ["R16_UINT"],
      "out_format": "R16G16B16A16_SFLOAT",
      "in_precision":  "INT16",
      "out_precision": "FP16",
      "memory_cost_per_pixel_bytes_in":  2,
      "memory_cost_per_pixel_bytes_out": 8,
      "scratch_per_pixel_bytes":         0,
      "preferred":  true
    },
    {
      "device":     "Metal",
      "engine":     "Halide",
      "artifact":   "demosaic_ahd-metal.metallib",
      "in_format":  ["R16_UINT"],
      "out_format": "R16G16B16A16_SFLOAT",
      "in_precision":  "INT16",
      "out_precision": "FP16",
      "memory_cost_per_pixel_bytes_in":  2,
      "memory_cost_per_pixel_bytes_out": 8,
      "scratch_per_pixel_bytes":         0,
      "preferred":  true
    },
    {
      "device":     "Hexagon",
      "engine":     "Halide",
      "artifact":   "demosaic_ahd-hexagon.so",
      "in_format":  ["R16_UINT"],
      "out_format": "R16G16B16A16_SFLOAT",
      "in_precision":  "INT16",
      "out_precision": "FP16",
      "memory_cost_per_pixel_bytes_in":  2,
      "memory_cost_per_pixel_bytes_out": 8,
      "scratch_per_pixel_bytes":         16,
      "preferred":  false
    },
    {
      "device":     "CPU",
      "engine":     "Halide",
      "artifact":   "demosaic_ahd-cpu.so",
      "in_format":  ["R16_UINT"],
      "out_format": "R16G16B16A16_SFLOAT",
      "in_precision":  "INT16",
      "out_precision": "FP16",
      "memory_cost_per_pixel_bytes_in":  2,
      "memory_cost_per_pixel_bytes_out": 8,
      "scratch_per_pixel_bytes":         0,
      "preferred":  false,
      "fallback":   true
    }
  ],
  "params":          [{"name":"pattern","type":"int","range":[0,3]}],
  "color_in":        "linear-camera-rgb",
  "color_out":       "linear-camera-rgb",
  "tile_safe":       true,
  "deterministic":   true
}
```

**Precision plan algorithm.**

```
for each edge e = (u -> v):
    e.format    = u.out_format  ∩  v.in_formats               # must overlap
    e.precision = u.out_precision                              # producer wins by default
    if v.in_precision != e.precision:
        insert ConvertNode(e.precision -> v.in_precision)      # FP16<->FP32 etc
return
```

Conversion nodes are themselves manifest-described and live on the same device as the producer. Inserting these ahead of time (rather than generating them on the fly) makes the cost visible to the memory planner.

**Why edge-as-Buffer-plus-format-plus-precision.** The unified `IBuffer` carries layout/format only. Precision is a higher-level concept — `IBuffer<R16G16B16A16_SFLOAT>` *is* FP16 in Vulkan terms, but a `BLOB` AHB has no format and the precision is whatever Halide says. Carrying precision separately on the edge keeps the manifest-author honest.

---

## 7. Cross-device hand-off

Cross-device boundaries are the **only** place cpipe accepts a copy (and even then only when zero-copy is impossible — e.g., NVIDIA discrete GPU has no AHB equivalent for sharing with the CPU encoder). [#02 §9] defines `Handoff`. The scheduler decides where handoffs go.

**Algorithm — hand-off insertion.** After device assignment:

```
for each edge (u -> v):
    if u.device == v.device:
        no handoff
    else:
        insert HandoffNode(u.device -> v.device, sync = timeline)
        update edge.buffer to a Buffer accessible by both devices
```

**Hand-off types and their cost.**

| From → To | Mechanism | Cost |
|-----------|-----------|------|
| Vulkan-GPU → Vulkan-GPU (same dev) | timeline value increment | 0 (just sync) |
| Vulkan-GPU → CPU (Halide CPU schedule) | host-mapped VkBuffer or vkCmdCopy to staging | one copy if discrete GPU |
| Vulkan-GPU → Hexagon-DSP | Halide HVX FastRPC handoff via shared AHB | one ARM↔DSP cache flush, no memcpy |
| Vulkan-GPU → Hexagon-NPU (HTP) | AHB import on QNN side ([#02 §3.4]) | 0 (zero-copy) on Snapdragon |
| Metal-GPU → CPU (Apple) | unified memory; just a sync | 0 |
| Metal-GPU → ANE (Apple) | IOSurface zero-copy | 0 |
| CPU → Vulkan-GPU | staging buffer copy | one copy |
| Discrete GPU (Linux) → CPU encoder | vkCmdCopyImageToBuffer | one copy |

The "one copy on cross-device boundaries" rule from the user's spec is therefore satisfied: within a device, edges are slot-shared; between devices, exactly one transfer.

**Scheduler invariant for handoffs.**

```cpp
// cpipe/scheduler/Handoff.cpp
void execute_handoff(Handoff& h, ICommandQueue& src_q, ICommandQueue& dst_q) {
    // 1. src signals timeline value
    src_q.signal_value(*h.sync, h.signal_value);
    // 2. Driver-level ordering: if same memory (zero-copy), no copy step
    if (!is_same_native_memory(*h.from, *h.to)) {
        // explicit copy (one-time cost, acknowledged)
        src_q.dispatch(make_copy_kernel(*h.from, *h.to), full_extent_of(h.to));
        src_q.signal_value(*h.sync, h.signal_value); // re-signal after copy
    }
    // 3. dst waits before its first dispatch
    dst_q.wait_value(*h.to, *h.sync, h.wait_value);
}
```

`is_same_native_memory` checks whether `from->native()` and `to->native()` resolve to the same underlying allocation (same AHB pointer, same VkDeviceMemory + offset, same IOSurface ref). This is a fast O(1) test.

---

## 8. Profile / trace integration

Editor-side ([#11]) needs per-node runtime ms and per-edge memory bytes. Three trace surfaces:

1. **Tracy.** `wolfpld/tracy` Frame Profiler — production-ready, supports OpenGL/Vulkan/D3D11/D3D12/Metal/OpenCL/CUDA. cpipe wraps each node body in a `ZoneScopedN("demosaic_ahd")` macro and each Vulkan dispatch in `TracyVkZone`. Tracy's "GPU zone" is the dispatch interval as measured by Vulkan timestamp queries.
2. **Perfetto.** Cluster-A's Linux/Android target. Mesa supports Perfetto producers natively (per Mesa docs on `docs.mesa3d.org/perfetto.html`). cpipe registers as a Perfetto producer that emits its own track + relays driver `render-stage` data sources. Per-node trace points use `PERFETTO_TE_TRACK_EVENT`. Output is a `.pftrace` file viewable in `ui.perfetto.dev`.
3. **Chrome Trace JSON.** `TFProf` (TaskFlow's profiler) emits a Chrome-Trace-JSON bundle on `tf::Executor::dump(...)` paired with our own per-node timer events. The cpipe editor opens this directly.

**Per-node runtime ms (live for editor).** For each node, the scheduler maintains a ring buffer of last N executions with ms timing. Editor polls `/api/pipeline/nodes/<id>/timing` over WebSocket [#11].

**Memory tracking.** The arena allocator records peak per-slot bytes per pipeline run. Exposed as `/api/pipeline/memory`.

---

## 9. Static topology + batch-only — and v2 future-proofing

**D6 Static topology.** After the JSON pipeline description is loaded and compiled into IR, the topology is frozen for the lifetime of the run. Parameters (e.g. exposure bias, white-balance gains) are mutable; nodes/edges are not. Conditional sub-graphs are out of scope.

**D5 Batch-only.** v1 processes one shot at a time (5–10 frames + 1 render). No streaming. The scheduler "run loop" is therefore an offline graph executor: load → plan → run → free.

**Why this simplifies dramatically.**

- No preemption, no dynamic graph mutation, no priority inheritance.
- Memory plan is exact, not heuristic; we never hit OOM mid-run if planning succeeded.
- All synchronization is forward-monotonic on a single timeline per device.
- The TaskFlow `tf::Taskflow` is built once and reused (or rebuilt only on parameter changes that trigger node-cost recomputation).

**Why architecture must NOT preclude v2 streaming.** D5 says batch-only v1 *but the buffer/scheduler design must not paint itself into a corner*. Mitigations:

- The `Handoff` struct already has timeline semantics — it works for streaming too.
- The `IBuffer` `sub_view` interface allows tile/strip access — a v2 streaming variant just creates a new view per token.
- `tf::Pipeline` is reserved for v2 (it natively supports staged tokens). v1 uses `tf::Taskflow` only.
- The scheduler's "run a graph" entrypoint takes a `RunContext` with a token id; v1 always uses token = 0.

---

## 10. Halide and Slang co-existence in one DAG

Halide kernels and Slang kernels are different `IComputeKernel` implementations ([#01 §7]). The scheduler is agnostic — it just calls `ICommandQueue::dispatch(kernel, extent)`. Two interactions deserve callouts:

1. **Halide's internal thread pool.** Halide CPU schedules use their own thread pool (`halide_do_par_for`). This pool is by default a `std::thread`-based pool sized to `halide_get_num_threads()` (max 256, default = number of cores). When TaskFlow is running, we need to either share the pool or accept oversubscription. The right answer is to override `halide_set_custom_do_par_for` / `halide_set_custom_do_loop_task` to dispatch through the TaskFlow `tf::Executor`. This is documented in `HalideRuntime.h` and is the same pattern Halide users employ to integrate with TBB / OpenMP.
2. **Vulkan command queue ordering.** When a Halide-Vulkan kernel and a Slang-Vulkan kernel run consecutively, both use the same `ICommandQueue` (the one cpipe owns); Halide's Vulkan runtime uses *its own* `VkQueue` by default. The fix is to construct the Halide pipeline with `Halide::Internal::JITSharedRuntime` configured to use cpipe's queue, or — for AOT — to provide a custom `halide_runtime_t` that forwards `halide_vulkan_acquire_context` to cpipe. This is a known integration point; we budget a week of integration in the implementation plan.

---

## 11. Pseudocode

### 11.1 Graph compilation (load → IR)

```cpp
// cpipe/scheduler/Compile.cpp
struct NodeIR {
    NodeId    id;
    std::string name;
    std::vector<EdgeId> in_edges, out_edges;
    DeviceKind device;
    Precision  in_prec, out_prec;
    PixelFormat in_fmt, out_fmt;
    const NodeManifest* manifest;
    std::shared_ptr<IComputeKernel> kernel; // resolved below
};
struct EdgeIR {
    EdgeId   id;
    NodeId   producer, consumer;
    PixelFormat format;
    Precision   precision;
    uint64_t    bytes_per_pixel;
    uint32_t    width, height;
    int         arena_slot = -1;            // assigned by memory planner
};

PipelineIR compile(const PipelineJSON& json,
                   const ManifestRegistry& reg,
                   const DeviceCaps& caps) {
    PipelineIR ir;
    // 1. Topology (static after this).
    for (auto& n : json.nodes) {
        ir.nodes.push_back(load_node_ir(n, reg));
    }
    for (auto& e : json.edges) {
        ir.edges.push_back(load_edge_ir(e, ir.nodes));
    }
    // 2. Device assignment.
    assign_devices(ir, caps);
    // 3. Precision planning -> insert ConvertNodes.
    plan_precision(ir);
    // 4. Hand-off insertion at cross-device boundaries.
    insert_handoffs(ir);
    // 5. Memory planning -> assign arena slots.
    plan_memory(ir, caps);
    // 6. Resolve concrete kernels.
    for (auto& n : ir.nodes) {
        n.kernel = resolve_kernel(n.manifest, n.device);
    }
    return ir;
}
```

### 11.2 Memory planner

```cpp
// cpipe/scheduler/MemoryPlan.cpp
struct ArenaPlan {
    uint64_t total_bytes;
    std::vector<uint64_t> slot_offsets;       // per-edge offset
    std::vector<uint64_t> slot_sizes;
};

ArenaPlan plan_memory(PipelineIR& ir, DeviceKind dev, const DeviceCaps& caps) {
    // 1. Compute liveness intervals per edge.
    auto topo = topological_sort(ir);
    std::vector<Interval> live(ir.edges.size());
    for (size_t i = 0; i < topo.size(); ++i) {
        auto& n = ir.nodes[topo[i]];
        if (n.device != dev) continue;
        for (auto eid : n.out_edges) live[eid].birth = i;
        for (auto eid : n.in_edges)  live[eid].death = std::max(live[eid].death, i + 1);
    }
    // 2. Build interference graph.
    InterferenceGraph g(ir.edges.size());
    for (auto& a : ir.edges) for (auto& b : ir.edges) {
        if (&a == &b) continue;
        if (overlaps(live[a.id], live[b.id])) g.add_edge(a.id, b.id);
    }
    // 3. First-fit graph coloring -> slot assignment.
    ArenaPlan plan;
    auto coloring = color_first_fit(g);
    for (size_t i = 0; i < ir.edges.size(); ++i) {
        auto& e = ir.edges[i];
        size_t slot = coloring[i];
        uint64_t need = uint64_t(e.width) * e.height * e.bytes_per_pixel;
        if (plan.slot_sizes.size() <= slot) plan.slot_sizes.resize(slot + 1);
        plan.slot_sizes[slot] = std::max(plan.slot_sizes[slot], need);
        e.arena_slot = (int)slot;
    }
    // 4. Layout slots into a single arena.
    uint64_t off = 0;
    for (auto sz : plan.slot_sizes) {
        plan.slot_offsets.push_back(off);
        off += align_up(sz, 256);  // alignment for VkBuffer / MTLBuffer
    }
    plan.total_bytes = off;
    if (plan.total_bytes > caps.max_alloc_bytes) {
        throw std::runtime_error("graph won't fit on device "
            + device_name(dev) + ": " + std::to_string(plan.total_bytes));
    }
    return plan;
}
```

### 11.3 Scheduling loop

```cpp
// cpipe/scheduler/Run.cpp
RunResult run(PipelineIR& ir, const Inputs& inputs, RunOptions opt) {
    tf::Executor exec(opt.cpu_threads);
    tf::Taskflow tf;
    std::vector<tf::Task> tasks(ir.nodes.size());

    // 1. Allocate per-device arenas.
    auto arenas = allocate_arenas(ir);
    // 2. Allocate per-device timeline semaphores.
    auto timelines = allocate_timelines(ir);
    // 3. For each NodeIR, create a TaskFlow task body that:
    //    - waits on inputs' timeline values
    //    - dispatches kernel
    //    - signals its own timeline value
    for (size_t i = 0; i < ir.nodes.size(); ++i) {
        auto& n = ir.nodes[i];
        tasks[i] = tf.emplace([&, i]() {
            auto& q = queue_for(n.device, n.id);
            for (auto eid : n.in_edges) {
                auto& e = ir.edges[eid];
                q.wait_value(buffer_for(e), *timelines[n.device], e.id);
            }
            n.kernel->bind_buffers(in_buffers_of(n));
            q.dispatch(*n.kernel, dispatch_extent(n));
            for (auto eid : n.out_edges) {
                q.signal_value(*timelines[n.device], ir.edges[eid].id);
            }
        });
    }
    // 4. Wire dependencies.
    for (auto& e : ir.edges) {
        tasks[e.producer].precede(tasks[e.consumer]);
    }
    // 5. Run.
    auto fut = exec.run(tf);
    fut.wait();
    return collect_results(ir);
}
```

In real code the `tasks[i]` body avoids capturing `i` by value into multiple lambdas; the actual implementation passes a context struct.

### 11.4 Cross-device hand-off (already shown in §7).

### 11.5 Manifest schema (already shown in §6).

---

## 12. Static-topology compilation cost

The compile phase (algorithm in §11.1) runs once at load. For a typical cpipe DAG of ~30 nodes:

- Topological sort: O(V+E), microseconds.
- Device assignment: O(V), microseconds.
- Precision planning: O(E), microseconds.
- Memory planning (interference + coloring): O(V²) worst case but for V<50 trivially fast — sub-millisecond.

Loading time is dominated by **kernel artifact loading**, not graph compilation. The Halide AOT artifacts are mmap'd; Slang artifacts are SPIR-V/MSL/DXIL blobs that go through `slang::ICompileRequest` once per session. Total cold-start: 200–500 ms on a Pixel 8 Pro for a representative pipeline (dominated by Vulkan pipeline cache warm-up).

---

## 13. Editor and observability hooks

The cpipe editor [#11] consumes the scheduler output via:

- `GET /api/pipeline/<id>/timing` — JSON `{node_id: ms[]}` of last N runs.
- `GET /api/pipeline/<id>/memory` — JSON `{device: {peak_bytes, slot_count}}` from arena plan.
- `GET /api/pipeline/<id>/trace.pftrace` — raw Perfetto trace download.
- `GET /api/pipeline/<id>/trace.json` — Chrome trace JSON (TaskFlow + cpipe events).
- `WS /ws/runs` — pushes per-node start/end events as the run progresses.

The scheduler's responsibility is to fill these endpoints; web-side concerns are in [#11]. The trace export is bounded so we do not blow the editor up — large traces auto-rotate after 1 GB.

---

## 14. Sources

- taskflow/taskflow `v4.0.0`, 2026-01-02 — `https://github.com/taskflow/taskflow/releases/tag/v4.0.0`
- taskflow/taskflow HEAD `93c307d160` 2026-05-03 — `https://github.com/taskflow/taskflow`
- TaskFlow QuickStart documentation root — `https://taskflow.github.io/`
- TaskFlow "Task-parallel Pipeline" — `https://taskflow.github.io/taskflow/TaskParallelPipeline.html`
- TaskFlow "Conditional Tasking" — `https://taskflow.github.io/taskflow/ConditionalTasking.html`
- TaskFlow Release 3.4.0 (Pipeline introduction) — `https://taskflow.github.io/taskflow/release-3-4-0.html`
- TaskFlow Release 3.6.0 — `https://taskflow.github.io/taskflow/release-3-6-0.html`
- TaskFlow "Compile Taskflow with CUDA" — `https://taskflow.github.io/taskflow/CompileTaskflowWithCUDA.html`
- TaskFlow Profiler `TFProf` documentation root — `https://taskflow.github.io/`
- oneTBB "Parallelizing Data Flow and Dependency Graphs" — `https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/Parallelizing_Flow_Graph.html`
- oneTBB `v2023.0.0` 2026-04-30 — `https://github.com/uxlfoundation/oneTBB/releases/tag/v2023.0.0`
- HPX `v1.11.0` 2025-06-29 — `https://github.com/STEllAR-GROUP/hpx/releases/tag/v1.11.0`
- HPX paper (arXiv:2401.03353) — `https://arxiv.org/abs/2401.03353`
- Folly Futures docs — `https://github.com/facebook/folly/blob/main/folly/docs/Futures.md`
- Folly Executors docs — `https://github.com/facebook/folly/blob/main/folly/docs/Executors.md`
- Galois project — `https://iss.oden.utexas.edu/?p=projects%2Fgalois`
- Halide v21 release `v21.0.0` 2025-09-16 — `https://github.com/halide/Halide/releases/tag/v21.0.0`
- Halide runtime headers (`HalideRuntime.h`) — `https://halide-lang.org/docs/_halide_runtime_8h.html`
- Halide custom thread-pool docs — `https://halide-lang.org/docs/_halide_runtime_8h_source.html`
- libdispatch (swiftlang/swift-corelibs-libdispatch) — `https://github.com/swiftlang/swift-corelibs-libdispatch`
- GStreamer GstTaskPool — `https://gstreamer.freedesktop.org/documentation/gstreamer/gsttaskpool.html`
- GStreamer scheduling modes — `https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/scheduling.html`
- vkdt `hanatos/vkdt` repo — `https://github.com/hanatos/vkdt` (read-only reference; license-tainted [D11])
- Khronos "Vulkan Timeline Semaphores" — `https://www.khronos.org/blog/vulkan-timeline-semaphores`
- Khronos VK_KHR_synchronization2 guide — `https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html`
- nvpro-samples `vk_timeline_semaphore` — `https://github.com/nvpro-samples/vk_timeline_semaphore`
- Mesa Perfetto Tracing — `https://docs.mesa3d.org/perfetto.html`
- IREE "Profiling GPUs using Vulkan" — `https://iree.dev/developers/performance/profiling-gpu-vulkan/`
- Tracy frame profiler `wolfpld/tracy` — `https://github.com/wolfpld/tracy`
- Perfetto docs — `https://perfetto.dev/docs/`
- VulkanProfiler (lstalmir) — `https://github.com/lstalmir/VulkanProfiler`
- Memory-aware Adaptive DAG Scheduling on Heterogeneous Architectures (arXiv:2503.22365) — `https://arxiv.org/html/2503.22365`
- "Limiting the memory footprint when dynamically scheduling DAGs" — `https://www.sciencedirect.com/science/article/abs/pii/S0743731518305112`
- HEFT (Topcuoglu et al.) — classic citation for heterogeneous-earliest-finish-time scheduling

---

## 15. See also

- [#01 — Compute Frameworks](01-compute-frameworks.md) — provides the `IComputeKernel` interface and the Halide-AOT / Slang-RHI choices the scheduler dispatches.
- [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md) — defines `IBuffer`, `IFence`, `ITimeline`, and the `Handoff` struct that the scheduler creates and consumes.
- [#04 — Mobile AI Inference](04-mobile-ai-inference.md) — picks ExecuteTorch / ONNX RT; cpipe routes those through `IComputeKernel` (or sibling `IInferenceKernel`) and the scheduler treats them as a device kind.
- [#05 — NPU Backends](05-npu-backends-zero-copy.md) — the NPU device kinds the scheduler can target and the QNN HTP fence semantics for `wait_value`.
- [#11 — Pipeline Editor](11-pipeline-editor-and-connectivity.md) — consumes the scheduler's `/api/pipeline/...` endpoints; the live timing and memory views.
- [#06 — Soft ISP Architectures](06-soft-isp-architectures.md) — comparative architecture lessons from vkdt's scheduler (similar shape, GPLv3 contamination prevents code reuse but design influence is fine).

---

## 16. Open questions

1. **Halide custom thread pool.** Concretely measure the cost of overriding `halide_set_custom_do_par_for` to forward to a TaskFlow executor. Risk: mutex contention on the cpipe Executor's queue when many small Halide tasks bombard it. If significant, a dedicated cpipe-CPU pool (separate from the orchestration pool) is acceptable.
2. **Vulkan multi-queue.** Modern flagship GPUs expose multiple compute queues (NVIDIA: graphics + 8 compute; Adreno: 1 graphics + 1 compute; Mali: usually 1; Apple Silicon: 1 — but no contention because the kernel is unified). The cpipe scheduler should default to **one compute queue** for simplicity and only opt into multi-queue when burst-fusion can demonstrably parallelize across them. Decision: defer to v1.5; v1 uses single compute queue per device.
3. **Conditional fallback paths.** D6 says static topology, but error recovery needs a fallback when (e.g.) Vulkan returns `VK_ERROR_OUT_OF_DEVICE_MEMORY` mid-run. We can use TaskFlow's `condition_task` to switch to the spill path. Schema-wise this is an "exception edge" — should it be in the manifest? Decision: keep recovery out of the manifest, handle at the scheduler level.
4. **Tracy + Perfetto duplication.** Tracy is great for per-thread CPU + per-Vulkan-queue GPU spans; Perfetto is great for system-wide views. Both have overhead. We should support both at compile time but enable only one at runtime. Default in dev builds: Tracy. Default in prod CLI: none.
5. **Scheduler determinism.** The TaskFlow work-stealing executor is non-deterministic — task order between independent tasks varies. This is fine for correctness (DAG ordering is preserved) but breaks bit-exact reproducibility of pipeline outputs *if* the kernels themselves are non-deterministic (e.g., atomic-add reductions). Cluster C [#07] needs to confirm cpipe's classical kernels are deterministic; AI nodes [#08] are typically deterministic at FP16 if the framework guarantees no atomics.
6. **Memory planning for tile-mode v2.** The current planner is image-mode (one slot per edge). v2 tile mode will need slot-per-tile or row-buffer style — Halide's "store_at + compute_at" gives us building blocks. Sketch this as an explicit "v2 PR" so the v1 planner doesn't get retrofitted prematurely.
7. **Hand-off between Halide-Hexagon and Vulkan compute.** The Halide-Hexagon offload mode talks FastRPC; on the same SoC the Vulkan-side AHB is the shared-memory primitive. The actual sequencing (Vulkan waits on Hexagon FastRPC fence FD; Hexagon DSP waits on Vulkan timeline) needs a verified prototype before committing to the path.
8. **TaskFlow GPU integration scope.** TaskFlow's `cudaFlow`/`syclFlow` are tempting for the desktop NVIDIA case, but they bind us to those ecosystems. For cpipe v1 we leave them off and use TaskFlow purely for CPU orchestration; GPU dispatch goes through cpipe's own `ICommandQueue`. Revisit in v1.5 if the desktop NVIDIA path benefits.

---

> Word count of body (excluding code, tables, and reference URLs): approximately 5,400 words.
