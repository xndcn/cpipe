// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cpipe::runtime {

class ComputeContext {
public:
    ComputeContext();

    void register_halide_filter(std::string aot_id, HalideFilterEntry entry);
    void register_halide_param_filter(std::string aot_id, HalideParamFilterEntry entry);

    [[nodiscard]] cpipe_status_t submit_halide(
        std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
        std::span<const std::shared_ptr<compute::IBuffer>> outputs);

    [[nodiscard]] cpipe_status_t submit_halide(
        std::string_view aot_id, std::initializer_list<std::shared_ptr<compute::IBuffer>> inputs,
        std::initializer_list<std::shared_ptr<compute::IBuffer>> outputs);

    [[nodiscard]] cpipe_status_t submit_halide_with_params(
        std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
        std::span<const std::shared_ptr<compute::IBuffer>> outputs,
        std::span<const std::byte> param_blob);

private:
    std::unordered_map<std::string, HalideFilterEntry> halide_filters_;
    std::unordered_map<std::string, HalideParamFilterEntry> halide_param_filters_;
};

}  // namespace cpipe::runtime
