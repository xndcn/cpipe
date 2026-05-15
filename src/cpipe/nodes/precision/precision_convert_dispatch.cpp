// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int precision_convert_r16u_to_f32(halide_buffer_t* input, halide_buffer_t* output);
extern "C" int precision_convert_f32_to_rgba16f(halide_buffer_t* input, halide_buffer_t* output);
extern "C" int precision_convert_rgba16f_to_rgba8(halide_buffer_t* input, halide_buffer_t* output);
extern "C" int precision_convert_rgba16f_to_rgba16u(halide_buffer_t* input,
                                                    halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("precision_convert_r16u_to_f32", &precision_convert_r16u_to_f32)
CPIPE_REGISTER_HALIDE_FILTER("precision_convert_f32_to_rgba16f", &precision_convert_f32_to_rgba16f)
CPIPE_REGISTER_HALIDE_FILTER("precision_convert_rgba16f_to_rgba8",
                             &precision_convert_rgba16f_to_rgba8)
CPIPE_REGISTER_HALIDE_FILTER("precision_convert_rgba16f_to_rgba16u",
                             &precision_convert_rgba16f_to_rgba16u)

void cpipe_link_builtin_precision_convert_halide() {}
