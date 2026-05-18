// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "Lut3dParams.hpp"

namespace {

extern "C" int color_3d_lut(halide_buffer_t* input, halide_buffer_t* lut, std::int32_t lut_size,
                            std::int32_t interpolation, halide_buffer_t* output);

int color_3d_lut_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                       halide_buffer_t* const* outputs, std::size_t n_outputs,
                       const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::Lut3dParamHeader)) {
        return CPIPE_BAD_INDEX;
    }

    cpipe::nodes::detail::Lut3dParamHeader header{};
    std::memcpy(&header, param_blob, sizeof(header));
    if (header.size < 2U || header.value_count == 0U || (header.value_count % 3U) != 0U) {
        return CPIPE_BAD_INDEX;
    }
    const auto expected_bytes =
        sizeof(header) + (static_cast<std::size_t>(header.value_count) * sizeof(float));
    if (param_blob_size != expected_bytes ||
        header.size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return CPIPE_BAD_INDEX;
    }

    const auto* lut_values =
        reinterpret_cast<const float*>(static_cast<const std::byte*>(param_blob) + sizeof(header));
    halide_dimension_t lut_dims[2]{};
    lut_dims[0].min = 0;
    lut_dims[0].extent = 3;
    lut_dims[0].stride = 1;
    lut_dims[0].flags = 0;
    lut_dims[1].min = 0;
    lut_dims[1].extent = static_cast<std::int32_t>(header.value_count / 3U);
    lut_dims[1].stride = 3;
    lut_dims[1].flags = 0;
    halide_buffer_t lut_buffer{};
    lut_buffer.host = reinterpret_cast<std::uint8_t*>(const_cast<float*>(lut_values));
    lut_buffer.type = halide_type_of<float>();
    lut_buffer.dimensions = 2;
    lut_buffer.dim = lut_dims;

    return color_3d_lut(inputs[0], &lut_buffer, static_cast<std::int32_t>(header.size),
                        static_cast<std::int32_t>(header.interpolation), outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("color_3d_lut", &color_3d_lut_param)

void cpipe_link_builtin_color_3d_lut_halide() {}
