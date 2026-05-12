// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cpipe/runtime/TaskExecutor.hpp"
#include "cpipe/sdk/cpipe_node.h"

struct halide_buffer_t;

namespace cpipe::runtime {

using HalideFilterEntry = int (*)(halide_buffer_t*, halide_buffer_t*);

struct HalideFilter {
    std::string_view id;
    HalideFilterEntry entry = nullptr;
};

class ComputeContext final {
public:
    explicit ComputeContext(TaskExecutor& executor,
                            std::span<const HalideFilter> halide_filters = {});
    ~ComputeContext();

    ComputeContext(const ComputeContext&) = delete;
    auto operator=(const ComputeContext&) -> ComputeContext& = delete;

    ComputeContext(ComputeContext&&) = delete;
    auto operator=(ComputeContext&&) -> ComputeContext& = delete;

    [[nodiscard]] auto native() noexcept -> cpipe_compute_t*;
    [[nodiscard]] auto native() const noexcept -> const cpipe_compute_t*;

    [[nodiscard]] auto submit_halide(const char* aot_id, const cpipe_buffer_t* const* inputs,
                                     std::size_t n_in, cpipe_buffer_t* const* outputs,
                                     std::size_t n_out) -> int;

private:
    TaskExecutor* executor_ = nullptr;
    std::unordered_map<std::string, HalideFilterEntry> halide_filters_;
    cpipe_compute_t* native_ = nullptr;
};

}  // namespace cpipe::runtime
