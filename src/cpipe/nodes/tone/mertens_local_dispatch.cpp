// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>

namespace {

struct MertensLocalParams {
    float weight_contrast;
    float weight_saturation;
    float weight_well_exposedness;
};

extern "C" int tone_mertens_local(halide_buffer_t* under, halide_buffer_t* normal,
                                  halide_buffer_t* over, float weight_contrast,
                                  float weight_saturation, float weight_well_exposedness,
                                  halide_buffer_t* output);

int tone_mertens_local_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                             halide_buffer_t* const* outputs, std::size_t n_outputs,
                             const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 3 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || inputs[1] == nullptr || inputs[2] == nullptr ||
        outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(MertensLocalParams)) {
        return CPIPE_BAD_INDEX;
    }
    const auto* params = static_cast<const MertensLocalParams*>(param_blob);
    return tone_mertens_local(inputs[0], inputs[1], inputs[2], params->weight_contrast,
                              params->weight_saturation, params->weight_well_exposedness,
                              outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("tone_mertens_local", &tone_mertens_local_param)

void cpipe_link_builtin_tone_mertens_local_halide() {}
