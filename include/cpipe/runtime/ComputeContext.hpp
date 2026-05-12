// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

struct halide_buffer_t;

namespace cpipe::compute {
class IBuffer;
}  // namespace cpipe::compute

namespace cpipe::runtime {

using HalideFilterEntry = int (*)(std::span<halide_buffer_t* const> inputs,
                                  std::span<halide_buffer_t* const> outputs);

class ComputeContext {
public:
    ComputeContext();
    ComputeContext(const ComputeContext&) = delete;
    auto operator=(const ComputeContext&) -> ComputeContext& = delete;
    ComputeContext(ComputeContext&&) = delete;
    auto operator=(ComputeContext&&) -> ComputeContext& = delete;
    ~ComputeContext();

    [[nodiscard]] auto handle() noexcept -> cpipe_compute_t*;
    [[nodiscard]] auto executor_worker_count() const noexcept -> std::size_t;
    auto register_halide(std::string_view aot_id, HalideFilterEntry entry) -> void;
    [[nodiscard]] auto submit_halide(std::string_view aot_id,
                                     std::span<const cpipe_buffer_t* const> inputs,
                                     std::span<cpipe_buffer_t* const> outputs) -> int;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto as_cpipe_buffer(compute::IBuffer& buffer) noexcept -> cpipe_buffer_t*;
[[nodiscard]] auto as_cpipe_buffer(const compute::IBuffer& buffer) noexcept
    -> const cpipe_buffer_t*;
[[nodiscard]] auto buffer_suite_v1() noexcept -> const cpipe_buffer_suite_v1&;
[[nodiscard]] auto compute_suite_v1() noexcept -> const cpipe_compute_suite_v1&;
[[nodiscard]] auto halide_custom_parallel_for_calls() noexcept -> std::uint64_t;

}  // namespace cpipe::runtime
