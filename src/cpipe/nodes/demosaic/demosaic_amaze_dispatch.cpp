// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/P1ParamDispatch.hpp"

extern "C" int demosaic_amaze(halide_buffer_t* input, halide_buffer_t* cfa_pattern,
                              halide_buffer_t* output);

namespace {

int demosaic_amaze_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                         halide_buffer_t* const* outputs, std::size_t n_outputs,
                         const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::DemosaicParams)) {
        return CPIPE_BAD_INDEX;
    }

    const auto* params = static_cast<const cpipe::nodes::detail::DemosaicParams*>(param_blob);
    std::array<std::int32_t, 4> pattern{params->cfa_pattern[0], params->cfa_pattern[1],
                                        params->cfa_pattern[2], params->cfa_pattern[3]};
    halide_dimension_t dim{0, 4, 1};
    halide_buffer_t cfa{};
    cfa.host = reinterpret_cast<std::uint8_t*>(pattern.data());
    cfa.type = halide_type_t{halide_type_int, 32};
    cfa.dimensions = 1;
    cfa.dim = &dim;
    return demosaic_amaze(inputs[0], &cfa, outputs[0]);
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("demosaic_amaze", &demosaic_amaze_param)

void cpipe_link_builtin_demosaic_amaze_halide() {}
