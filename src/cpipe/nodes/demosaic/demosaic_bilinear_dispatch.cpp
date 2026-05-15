// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int demosaic_bilinear(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("demosaic_bilinear", &demosaic_bilinear)

void cpipe_link_builtin_demosaic_bilinear_halide() {}
