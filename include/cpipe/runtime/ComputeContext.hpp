// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cpipe/sdk/cpipe_node.h>

namespace cpipe::runtime {

class ComputeContext {
public:
    void register_halide(std::string aot_id, HalideFilterEntry entry);

    [[nodiscard]] cpipe_status_t submit_halide(
        std::string_view aot_id, std::span<const compute::IBuffer* const> inputs,
        std::span<compute::IBuffer* const> outputs) const;

private:
    std::unordered_map<std::string, HalideFilterEntry> halide_entries_;
};

}  // namespace cpipe::runtime
