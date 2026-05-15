# P2 T21 Full Pipeline Tracy Evidence

Date: 2026-05-16

## Local Full-Pipeline Smokes

Debug binary smoke commands were run from the repo root:

- `build/linux-debug/src/cpipe/cli/cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o /tmp/cpipe_p2_sdr.heif`
- `build/linux-debug/src/cpipe/cli/cpipe run tests/corpus/pixel8pro.dng -p examples/pipelines/full-classic-pipeline-hdr.cpipe.json -o /tmp/cpipe_p2_hdr.heif`
- `build/linux-debug/src/cpipe/cli/cpipe run tests/corpus/pixel8pro-qbc.dng -p examples/pipelines/full-classic-pipeline.cpipe.json -o /tmp/cpipe_p2_qbc.heif`

All three exited 0. `/tmp/heif-info -d` confirmed SDR CICP `(1, 13, 1)`, HDR
Main10 CICP `(9, 16, 9)` with `mdcv` / `clli`, and synthetic QBC SDR output
dimensions `32x32`.

## Tracy Span Audit

The T21 source span points present at close are:

- `Pipeline::run`
- `Scheduler::dispatch_node`
- `ComputeContext::submit_halide`
- `ComputeContext::submit_halide_with_params`
- `VulkanCommandBuffer::submit`
- `VulkanDevicePlane::wait_timeline`
- `MemoryPlanner::plan_graph_coloring`
- `PrecisionPlanner::auto_insert`

`color.scene_linear_to_display` ships through the CPU OCIO processor path per
P2-PD-74, so the planned `OcioVulkanProcessor::compute_pass` span is not
present in the T21 runtime path. A binary `.tracy` capture was not produced in
this workspace because no Tracy capture tooling is checked into the repo; this
is recorded as a P2-PD-76 slip rather than inferred from tests.
