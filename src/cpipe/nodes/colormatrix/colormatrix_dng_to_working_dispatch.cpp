// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/Float16.hpp"
#include "../detail/P1ParamDispatch.hpp"

namespace {

int colormatrix_dng_to_working_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                     halide_buffer_t* const* outputs, std::size_t n_outputs,
                                     const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::ColormatrixParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const cpipe::nodes::detail::ColormatrixParams*>(param_blob);
    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    const auto* in = reinterpret_cast<const std::uint16_t*>(input->host);
    auto* out = reinterpret_cast<std::uint16_t*>(output->host);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::array<float, 3> rgb{};
            for (int c = 0; c < 3; ++c) {
                const auto offset = (y * input->dim[1].stride) + (x * input->dim[0].stride) +
                                    (c * input->dim[2].stride);
                rgb[static_cast<std::size_t>(c)] = cpipe::nodes::detail::half_to_float(in[offset]);
            }
            const std::array<float, 3> mapped{
                params->transform[0] * rgb[0] + params->transform[1] * rgb[1] +
                    params->transform[2] * rgb[2],
                params->transform[3] * rgb[0] + params->transform[4] * rgb[1] +
                    params->transform[5] * rgb[2],
                params->transform[6] * rgb[0] + params->transform[7] * rgb[1] +
                    params->transform[8] * rgb[2],
            };
            for (int c = 0; c < 3; ++c) {
                const auto out_offset = (y * output->dim[1].stride) + (x * output->dim[0].stride) +
                                        (c * output->dim[2].stride);
                out[out_offset] =
                    cpipe::nodes::detail::float_to_half(mapped[static_cast<std::size_t>(c)]);
            }
            const auto alpha_in = (y * input->dim[1].stride) + (x * input->dim[0].stride) +
                                  (3 * input->dim[2].stride);
            const auto alpha_out = (y * output->dim[1].stride) + (x * output->dim[0].stride) +
                                   (3 * output->dim[2].stride);
            out[alpha_out] = in[alpha_in];
        }
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("colormatrix_dng_to_working", &colormatrix_dng_to_working_param)

void cpipe_link_builtin_colormatrix_dng_to_working_halide() {}
