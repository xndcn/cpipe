// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <cmath>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/P1ParamDispatch.hpp"

namespace {

struct AxisSample {
    std::uint32_t low{0};
    std::uint32_t high{0};
    float weight{0.0F};
};

AxisSample sample_axis(double coord, std::uint32_t points) {
    if (points <= 1U || coord <= 0.0) {
        return AxisSample{};
    }
    const auto last = static_cast<double>(points - 1U);
    if (coord >= last) {
        const auto edge = points - 1U;
        return AxisSample{.low = edge, .high = edge, .weight = 0.0F};
    }
    const auto low = static_cast<std::uint32_t>(std::floor(coord));
    return AxisSample{.low = low,
                      .high = low + 1U,
                      .weight = static_cast<float>(coord - static_cast<double>(low))};
}

float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

const cpipe::nodes::detail::GainMapDispatchPlane* select_map(
    const cpipe::nodes::detail::GainMapDispatchHeader& header,
    const cpipe::nodes::detail::GainMapDispatchPlane* maps, std::uint32_t x, std::uint32_t y) {
    if (header.map_count == 1U) {
        return &maps[0];
    }

    std::uint32_t plane = ((y & 1U) * 2U) + (x & 1U);
    if (header.cfa_repeat[0] == 4U && header.cfa_repeat[1] == 4U) {
        plane = (((y % 4U) / 2U) * 2U) + ((x % 4U) / 2U);
    }
    for (std::uint32_t i = 0; i < header.map_count; ++i) {
        if (maps[i].plane == plane) {
            return &maps[i];
        }
    }
    return nullptr;
}

float sample_gain(const cpipe::nodes::detail::GainMapDispatchPlane& map, const float* gains,
                  std::uint32_t x, std::uint32_t y) {
    const auto grid_y = (static_cast<double>(y) - map.map_origin_v) / map.map_spacing_v;
    const auto grid_x = (static_cast<double>(x) - map.map_origin_h) / map.map_spacing_h;
    const auto sy = sample_axis(grid_y, map.map_points_v);
    const auto sx = sample_axis(grid_x, map.map_points_h);
    const auto row_stride = map.map_points_h;
    const auto g00 = gains[(sy.low * row_stride) + sx.low];
    const auto g10 = gains[(sy.low * row_stride) + sx.high];
    const auto g01 = gains[(sy.high * row_stride) + sx.low];
    const auto g11 = gains[(sy.high * row_stride) + sx.high];
    return lerp(lerp(g00, g10, sx.weight), lerp(g01, g11, sx.weight), sy.weight);
}

int lens_shading_gainmap_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                               halide_buffer_t* const* outputs, std::size_t n_outputs,
                               const void* param_blob, std::size_t param_blob_size) {
    using cpipe::nodes::detail::GainMapDispatchHeader;
    using cpipe::nodes::detail::GainMapDispatchPlane;

    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(GainMapDispatchHeader)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* header = static_cast<const GainMapDispatchHeader*>(param_blob);
    if (header->map_count == 0U || header->map_count > 4U) {
        return CPIPE_NEED_METADATA;
    }
    const auto map_bytes =
        static_cast<std::size_t>(header->map_count) * sizeof(GainMapDispatchPlane);
    const auto map_offset = sizeof(GainMapDispatchHeader);
    if (param_blob_size < map_offset + map_bytes) {
        return CPIPE_BAD_INDEX;
    }
    const auto* maps = reinterpret_cast<const GainMapDispatchPlane*>(
        static_cast<const std::byte*>(param_blob) + map_offset);
    const auto* gains = reinterpret_cast<const float*>(static_cast<const std::byte*>(param_blob) +
                                                       map_offset + map_bytes);
    const auto gain_bytes = param_blob_size - map_offset - map_bytes;

    for (std::uint32_t i = 0; i < header->map_count; ++i) {
        const auto required =
            (static_cast<std::uint64_t>(maps[i].gain_offset) + maps[i].gain_count) * sizeof(float);
        if (maps[i].map_points_v == 0U || maps[i].map_points_h == 0U || maps[i].gain_count == 0U ||
            required > gain_bytes) {
            return CPIPE_BAD_INDEX;
        }
    }

    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    const auto* in = reinterpret_cast<const float*>(input->host);
    auto* out = reinterpret_cast<float*>(output->host);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* map = select_map(*header, maps, static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(y));
            if (map == nullptr) {
                return CPIPE_NEED_METADATA;
            }
            const auto* map_gains = gains + map->gain_offset;
            const auto in_offset = (y * input->dim[1].stride) + (x * input->dim[0].stride);
            const auto out_offset = (y * output->dim[1].stride) + (x * output->dim[0].stride);
            out[out_offset] =
                in[in_offset] * sample_gain(*map, map_gains, static_cast<std::uint32_t>(x),
                                            static_cast<std::uint32_t>(y));
        }
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("lens_shading_gainmap", &lens_shading_gainmap_param)

void cpipe_link_builtin_lens_shading_gainmap_halide() {}
