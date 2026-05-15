// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int sharpen_edge_aware_usm(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("sharpen_edge_aware_usm", &sharpen_edge_aware_usm)

void cpipe_link_builtin_sharpen_edge_aware_usm_halide() {}
