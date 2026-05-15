// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>

namespace {

struct Bm3dParams {
    float sigma;
};

extern "C" int denoise_bm3d_step1(halide_buffer_t* input, float sigma, halide_buffer_t* output);
extern "C" int denoise_bm3d_step2(halide_buffer_t* input, float sigma, halide_buffer_t* output);

int denoise_bm3d_step1_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                             halide_buffer_t* const* outputs, std::size_t n_outputs,
                             const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(Bm3dParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const Bm3dParams*>(param_blob);
    return denoise_bm3d_step1(inputs[0], params->sigma, outputs[0]);
}

int denoise_bm3d_step2_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                             halide_buffer_t* const* outputs, std::size_t n_outputs,
                             const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(Bm3dParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const Bm3dParams*>(param_blob);
    return denoise_bm3d_step2(inputs[0], params->sigma, outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("denoise_bm3d_step1", &denoise_bm3d_step1_param)
CPIPE_REGISTER_HALIDE_PARAM_FILTER("denoise_bm3d_step2", &denoise_bm3d_step2_param)

void cpipe_link_builtin_denoise_bm3d_halide() {}
