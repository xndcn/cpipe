// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int demosaic_quad_bayer_remosaic(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("demosaic_quad_bayer_remosaic", &demosaic_quad_bayer_remosaic)

void cpipe_link_builtin_demosaic_quad_bayer_remosaic_halide() {}
