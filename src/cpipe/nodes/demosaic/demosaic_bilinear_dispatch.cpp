// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>

#include "../detail/P1ParamDispatch.hpp"

extern "C" int demosaic_bilinear(halide_buffer_t* input, halide_buffer_t* cfa_pattern,
                                 halide_buffer_t* output);

namespace {

halide_buffer_t cfa_buffer(std::array<std::int32_t, 4>& pattern, halide_dimension_t& dim) {
    dim = halide_dimension_t{0, 4, 1};
    halide_buffer_t buffer{};
    buffer.host = reinterpret_cast<std::uint8_t*>(pattern.data());
    buffer.type = halide_type_t{halide_type_int, 32};
    buffer.dimensions = 1;
    buffer.dim = &dim;
    return buffer;
}

std::array<std::int32_t, 4> cfa_pattern_from_blob(const void* param_blob) {
    const auto* params = static_cast<const cpipe::nodes::detail::DemosaicParams*>(param_blob);
    return {params->cfa_pattern[0], params->cfa_pattern[1], params->cfa_pattern[2],
            params->cfa_pattern[3]};
}

int demosaic_bilinear_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                            halide_buffer_t* const* outputs, std::size_t n_outputs,
                            const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size < sizeof(cpipe::nodes::detail::DemosaicParams)) {
        return CPIPE_BAD_INDEX;
    }
    auto pattern = cfa_pattern_from_blob(param_blob);
    halide_dimension_t dim{};
    auto cfa = cfa_buffer(pattern, dim);
    return demosaic_bilinear(inputs[0], &cfa, outputs[0]);
}

int demosaic_bilinear_rggb(halide_buffer_t* input, halide_buffer_t* output) {
    std::array<std::int32_t, 4> pattern{0, 1, 1, 2};
    halide_dimension_t dim{};
    auto cfa = cfa_buffer(pattern, dim);
    return demosaic_bilinear(input, &cfa, output);
}

}  // namespace

CPIPE_REGISTER_HALIDE_FILTER("demosaic_bilinear", &demosaic_bilinear_rggb)
CPIPE_REGISTER_HALIDE_PARAM_FILTER("demosaic_bilinear", &demosaic_bilinear_param)

void cpipe_link_builtin_demosaic_bilinear_halide() {}
