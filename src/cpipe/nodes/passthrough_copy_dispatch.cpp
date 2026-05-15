// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HalideFilterRegistry.hpp>

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);

CPIPE_REGISTER_HALIDE_FILTER("passthrough_copy", &passthrough_copy)

void cpipe_link_builtin_passthrough_halide() {}
