// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/MemoryPlanner.hpp>

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace cpipe::runtime {
namespace {

std::uint64_t pixel_count(const compute::BufferLayout& layout) noexcept {
    if (layout.ndim < 2) {
        return 0;
    }
    return static_cast<std::uint64_t>(layout.dims[0]) * layout.dims[1];
}

std::uint64_t output_bytes_per_pixel(const cpipe_plugin_desc_t* desc) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return 0;
    }
    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    return manifest.value("compute", nlohmann::json::object())
        .value("out_pixel_bytes", std::uint64_t{0});
}

}  // namespace

MemoryPlan MemoryPlanner::plan(compute::BufferLayout input_layout,
                               std::span<const cpipe_plugin_desc_t* const> nodes) {
    const auto input_bytes = input_layout.size_bytes();
    const auto pixels = pixel_count(input_layout);
    std::uint64_t peak = input_bytes;

    for (const auto* node : nodes) {
        const auto output_bytes = pixels * output_bytes_per_pixel(node);
        peak = std::max(peak, input_bytes + output_bytes);
    }
    return MemoryPlan{.peak_bytes = peak};
}

}  // namespace cpipe::runtime
