# P3 T1 OCIO Vulkan Evidence

Date: 2026-05-18

## Local Vulkan OCIO

`CPIPE_VULKAN_AVAILABLE=ON ASAN_OPTIONS=detect_leaks=0 build/linux-debug/tests/unit/test_ocio_vulkan_processor --reporter compact`

- Result: passed, 35 assertions.
- Device: `Intel(R) UHD Graphics 770 (ADL-S GT1)`, Vulkan API 1.3.255.
- Coverage: OCIO v0.2 scene-linear Rec.2020 to sRGB emits Vulkan GLSL through `OCIO::GpuShaderDesc`, glslang compiles it to SPIR-V at runtime, `OcioVulkanProcessor::compute_pass` binds a cpipe-owned compute pipeline through `VulkanCommandBuffer`, dispatches over `VulkanImage` storage images, and matches CPU OCIO within 0.5 LSB.

## Node Route

`CPIPE_VULKAN_AVAILABLE=ON ASAN_OPTIONS=detect_leaks=0 build/linux-debug/tests/unit/test_node_color_scene_linear_to_display --reporter compact`

- Result: passed, 58 assertions.
- Coverage: `com.cpipe.color.scene_linear_to_display` reaches `host->get_ocio_processor` and `submit_ocio_processor`; with Vulkan-backed input/output images and `CPIPE_VULKAN_AVAILABLE=ON`, `HostContext` uses `OcioVulkanProcessor::compute_pass`.

## Tracy Status

The T1 source span point is present:

- `OcioVulkanProcessor::compute_pass`

A binary `.tracy` capture was not produced in this workspace because no Tracy capture tooling is checked into the repo. P3-PD-46 already carries binary Tracy capture in CI as local-only evidence; the commands above verify the source span and Vulkan compute dispatch path.
