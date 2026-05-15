# P2 T2 Vulkan Dispatch Evidence

Date: 2026-05-15

## Local Vulkan Dispatch

`CPIPE_VULKAN_AVAILABLE=ON ASAN_OPTIONS=detect_leaks=0 build/linux-debug/tests/unit/test_vulkan_command_buffer --reporter compact`

- Result: passed, 5 assertions.
- Device: `Intel(R) UHD Graphics 770 (ADL-S GT1)`, Vulkan API 1.3.255.
- Coverage: `VulkanCommandBuffer` records `vkCmdBindPipeline`, `vkCmdBindDescriptorSets`, `vkCmdDispatch`, submits to the cpipe-owned queue, signals a timeline semaphore, waits through `VulkanDevicePlane::wait_timeline`, and reads back the expected storage-buffer value.

## Demosaic Regression

`CPIPE_VULKAN_AVAILABLE=ON ASAN_OPTIONS=detect_leaks=0 build/linux-debug/tests/unit/test_node_demosaic_bilinear_vulkan --reporter compact`

- Result: passed, 75 assertions.
- Device: `Intel(R) UHD Graphics 770 (ADL-S GT1)`, Vulkan API 1.3.255.
- Scope: regression only. Per P2-PD-59, Halide v21 still owns the command-buffer path for `demosaic.bilinear` Vulkan AOT.

## Halide Handoff Check

- `add_halide_library()` in the Halide v21 CMake package appends `-no_runtime`.
- The generated object `demosaic_bilinear-x86-64-linux-vulkan-vk_float16-no_runtime.o` still references `halide_vulkan_initialize_kernels`, `halide_vulkan_run`, `halide_vulkan_finalize_kernels`, and `halide_vulkan_device_interface`.
- Local Halide v21 headers/docs expose overrideable Vulkan runtime entry points but no documented `HL_VULKAN_NO_HOST_RUNTIME` switch or stable descriptor-layout handoff for binding Halide AOT SPIR-V directly from cpipe.

## Tracy Status

The T2 source span points are present:

- `VulkanCommandBuffer::submit`
- `VulkanDevicePlane::wait_timeline`

A binary Tracy capture was not produced in this workspace. The evidence above verifies the Vulkan dispatch path; P2-PD-59 records the Halide demosaic handoff carry.
