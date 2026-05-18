// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

namespace {

struct GuidedFilterParams {
    std::int32_t radius;
    float eps;
};

extern "C" int denoise_guided_filter(halide_buffer_t* input, std::int32_t radius, float eps,
                                     halide_buffer_t* output);

int denoise_guided_filter_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                halide_buffer_t* const* outputs, std::size_t n_outputs,
                                const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(GuidedFilterParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const GuidedFilterParams*>(param_blob);
    return denoise_guided_filter(inputs[0], params->radius, params->eps, outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("denoise_guided_filter", &denoise_guided_filter_param)

void cpipe_link_builtin_denoise_guided_filter_halide() {}
