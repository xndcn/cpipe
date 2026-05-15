// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int denoise_guided_filter(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("denoise_guided_filter", &denoise_guided_filter)

void cpipe_link_builtin_denoise_guided_filter_halide() {}
