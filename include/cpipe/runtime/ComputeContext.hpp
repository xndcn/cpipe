// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>

#include <cstddef>
#include <string_view>
#include <taskflow/taskflow.hpp>
#include <unordered_map>

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

using HalideFilterEntry = int (*)(halide_buffer_t*, halide_buffer_t*);

class ComputeContext {
public:
    ComputeContext();

    [[nodiscard]] tf::Executor& executor() noexcept;

    int submit_halide(std::string_view aot_id, const cpipe_buffer_t* const* inputs,
                      std::size_t n_in, cpipe_buffer_t* const* outputs, std::size_t n_out);

private:
    tf::Executor executor_;
    std::unordered_map<std::string_view, HalideFilterEntry> halide_filters_;
};

}  // namespace cpipe::runtime
