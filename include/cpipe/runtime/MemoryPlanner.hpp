// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/sdk/cpipe_node.h>

#include <cstdint>
#include <span>

namespace cpipe::runtime {

struct MemoryPlan {
    std::uint64_t peak_bytes{0};
};

class MemoryPlanner {
public:
    [[nodiscard]] static MemoryPlan plan(compute::BufferLayout input_layout,
                                         std::span<const cpipe_plugin_desc_t* const> nodes);
};

}  // namespace cpipe::runtime
