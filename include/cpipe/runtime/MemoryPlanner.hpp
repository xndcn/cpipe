// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/BufferLayout.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace cpipe::runtime {

struct MemoryGraphEdge {
    std::size_t from{0};
    std::size_t to{0};
};

struct MemoryAssignment {
    std::size_t buffer_id{0};
    std::size_t node_index{0};
    std::size_t physical_slot{0};
    std::uint64_t size_bytes{0};
};

struct MemoryPlan {
    std::uint64_t peak_bytes{0};
    std::size_t physical_slot_count{0};
    std::vector<MemoryAssignment> assignments;
};

class MemoryPlanner {
public:
    [[nodiscard]] static MemoryPlan plan(compute::BufferLayout input_layout,
                                         std::span<const cpipe_plugin_desc_t* const> nodes);
    [[nodiscard]] static MemoryPlan plan_graph(compute::BufferLayout input_layout,
                                               std::span<const cpipe_plugin_desc_t* const> nodes,
                                               std::span<const MemoryGraphEdge> edges);
};

}  // namespace cpipe::runtime
