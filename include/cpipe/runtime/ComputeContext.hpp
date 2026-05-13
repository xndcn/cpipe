// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cpipe::runtime {

using HalideFilterEntry = int (*)(halide_buffer_t* input, halide_buffer_t* output);

class ComputeContext {
public:
    void register_halide_filter(std::string aot_id, HalideFilterEntry entry);

    [[nodiscard]] cpipe_status_t submit_halide(
        std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
        std::span<const std::shared_ptr<compute::IBuffer>> outputs);

    [[nodiscard]] cpipe_status_t submit_halide(
        std::string_view aot_id, std::initializer_list<std::shared_ptr<compute::IBuffer>> inputs,
        std::initializer_list<std::shared_ptr<compute::IBuffer>> outputs);

private:
    std::unordered_map<std::string, HalideFilterEntry> halide_filters_;
};

}  // namespace cpipe::runtime
