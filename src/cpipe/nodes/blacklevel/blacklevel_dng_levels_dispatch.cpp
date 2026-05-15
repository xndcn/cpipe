// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/P1ParamDispatch.hpp"

namespace {

int blacklevel_dng_levels_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                halide_buffer_t* const* outputs, std::size_t n_outputs,
                                const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::BlacklevelParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const cpipe::nodes::detail::BlacklevelParams*>(param_blob);
    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    const auto* in = reinterpret_cast<const float*>(input->host);
    auto* out = reinterpret_cast<float*>(output->host);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto cfa_index =
                ((static_cast<std::uint32_t>(y) & 1U) * 2U) + (static_cast<std::uint32_t>(x) & 1U);
            const auto channel = std::min<std::uint8_t>(params->cfa_pattern[cfa_index], 3U);
            const auto black = params->black_level[channel];
            const auto denominator = static_cast<float>(params->white_level) - black;
            if (denominator <= 0.0F) {
                return CPIPE_NEED_METADATA;
            }
            const auto in_offset = (y * input->dim[1].stride) + (x * input->dim[0].stride);
            const auto out_offset = (y * output->dim[1].stride) + (x * output->dim[0].stride);
            out[out_offset] = std::clamp((in[in_offset] - black) / denominator, 0.0F, 1.0F);
        }
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("blacklevel_dng_levels", &blacklevel_dng_levels_param)

void cpipe_link_builtin_blacklevel_dng_levels_halide() {}
