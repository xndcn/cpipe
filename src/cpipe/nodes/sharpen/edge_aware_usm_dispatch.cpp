// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

namespace {

struct EdgeAwareUsmParams {
    float strength;
    std::int32_t radius;
    float threshold;
};

extern "C" int sharpen_edge_aware_usm(halide_buffer_t* input, float strength, std::int32_t radius,
                                      float threshold, halide_buffer_t* output);

int sharpen_edge_aware_usm_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                 halide_buffer_t* const* outputs, std::size_t n_outputs,
                                 const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(EdgeAwareUsmParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const EdgeAwareUsmParams*>(param_blob);
    return sharpen_edge_aware_usm(inputs[0], params->strength, params->radius, params->threshold,
                                  outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("sharpen_edge_aware_usm", &sharpen_edge_aware_usm_param)

void cpipe_link_builtin_sharpen_edge_aware_usm_halide() {}
