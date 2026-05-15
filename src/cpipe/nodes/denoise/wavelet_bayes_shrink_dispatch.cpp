// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int denoise_wavelet_bayes_shrink(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("denoise_wavelet_bayes_shrink", &denoise_wavelet_bayes_shrink)

void cpipe_link_builtin_denoise_wavelet_bayes_shrink_halide() {}
