// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/nodes/Passthrough.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace cpipe::nodes {
namespace {

int passthrough_copy(const runtime::HalideBufferView* const* inputs, std::size_t n_in,
                     runtime::HalideBufferView* const* outputs, std::size_t n_out) {
    if (n_in != 1 || n_out != 1) {
        return CPIPE_BAD_INDEX;
    }
    const auto* input = inputs[0];
    auto* output = outputs[0];
    if (input == nullptr || output == nullptr || input->host == nullptr || output->host == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    if (input->size_bytes != output->size_bytes) {
        return CPIPE_FAILED;
    }

    std::memcpy(output->host, input->host, static_cast<std::size_t>(input->size_bytes));
    return CPIPE_OK;
}

}  // namespace

void register_passthrough_halide(runtime::ComputeContext& context) {
    context.register_halide("passthrough_copy", &passthrough_copy);
}

}  // namespace cpipe::nodes
