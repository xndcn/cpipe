// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>

namespace {

extern "C" int tone_mertens_local(halide_buffer_t* under, halide_buffer_t* normal,
                                  halide_buffer_t* over, halide_buffer_t* output);

int tone_mertens_local_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                             halide_buffer_t* const* outputs, std::size_t n_outputs, const void*,
                             std::size_t) {
    if (n_inputs != 3 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || inputs[1] == nullptr || inputs[2] == nullptr ||
        outputs[0] == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    return tone_mertens_local(inputs[0], inputs[1], inputs[2], outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("tone_mertens_local", &tone_mertens_local_param)

void cpipe_link_builtin_tone_mertens_local_halide() {}
