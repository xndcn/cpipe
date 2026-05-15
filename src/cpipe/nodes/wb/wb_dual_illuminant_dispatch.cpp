// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/Float16.hpp"
#include "../detail/P1ParamDispatch.hpp"

namespace {

int wb_dual_illuminant_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                             halide_buffer_t* const* outputs, std::size_t n_outputs,
                             const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::WbParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const cpipe::nodes::detail::WbParams*>(param_blob);
    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    const auto* in = reinterpret_cast<const std::uint16_t*>(input->host);
    auto* out = reinterpret_cast<std::uint16_t*>(output->host);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 4; ++c) {
                const auto offset = (y * input->dim[1].stride) + (x * input->dim[0].stride) +
                                    (c * input->dim[2].stride);
                const auto out_offset = (y * output->dim[1].stride) + (x * output->dim[0].stride) +
                                        (c * output->dim[2].stride);
                if (c == 3) {
                    out[out_offset] = in[offset];
                    continue;
                }
                out[out_offset] = cpipe::nodes::detail::float_to_half(
                    cpipe::nodes::detail::half_to_float(in[offset]) / params->as_shot_neutral[c]);
            }
        }
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("wb_dual_illuminant", &wb_dual_illuminant_param)

void cpipe_link_builtin_wb_dual_illuminant_halide() {}
