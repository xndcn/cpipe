// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>

namespace {

struct ReinhardParams {
    float white_point;
};

extern "C" int tone_reinhard(halide_buffer_t* input, float white_point, halide_buffer_t* output);

int tone_reinhard_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                        halide_buffer_t* const* outputs, std::size_t n_outputs,
                        const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(ReinhardParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const ReinhardParams*>(param_blob);
    return tone_reinhard(inputs[0], params->white_point, outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("tone_reinhard", &tone_reinhard_param)

void cpipe_link_builtin_tone_reinhard_halide() {}
