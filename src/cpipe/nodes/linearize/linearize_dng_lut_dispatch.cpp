// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../detail/P1ParamDispatch.hpp"

namespace {

int linearize_dng_lut_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                            halide_buffer_t* const* outputs, std::size_t n_outputs,
                            const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::LinearizeParamsHeader)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* header =
        static_cast<const cpipe::nodes::detail::LinearizeParamsHeader*>(param_blob);
    const auto expected_size =
        sizeof(*header) + (static_cast<std::size_t>(header->table_size) * sizeof(std::uint16_t));
    if (header->table_size == 0 || param_blob_size < expected_size) {
        return CPIPE_NEED_METADATA;
    }
    const auto* table = reinterpret_cast<const std::uint16_t*>(
        static_cast<const std::byte*>(param_blob) + sizeof(*header));

    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    const auto* in = reinterpret_cast<const std::uint16_t*>(input->host);
    auto* out = reinterpret_cast<float*>(output->host);
    const auto last = static_cast<std::size_t>(header->table_size - 1U);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto in_offset = (y * input->dim[1].stride) + (x * input->dim[0].stride);
            const auto out_offset = (y * output->dim[1].stride) + (x * output->dim[0].stride);
            const auto index = std::min<std::size_t>(in[in_offset], last);
            out[out_offset] = static_cast<float>(table[index]);
        }
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("linearize_dng_lut", &linearize_dng_lut_param)

void cpipe_link_builtin_linearize_dng_lut_halide() {}
