// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>

namespace {

struct WaveletBayesShrinkParams {
    float chroma_strength;
};

extern "C" int denoise_wavelet_bayes_shrink(halide_buffer_t* input, float chroma_strength,
                                            halide_buffer_t* output);

int denoise_wavelet_bayes_shrink_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                       halide_buffer_t* const* outputs, std::size_t n_outputs,
                                       const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(WaveletBayesShrinkParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const WaveletBayesShrinkParams*>(param_blob);
    return denoise_wavelet_bayes_shrink(inputs[0], params->chroma_strength, outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("denoise_wavelet_bayes_shrink",
                                   &denoise_wavelet_bayes_shrink_param)

void cpipe_link_builtin_denoise_wavelet_bayes_shrink_halide() {}
