// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int tone_filmic_rgb(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("tone_filmic_rgb", &tone_filmic_rgb)

void cpipe_link_builtin_tone_filmic_rgb_halide() {}
