// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/runtime/MemoryPlanner.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::PixelFormat;
using cpipe::runtime::MemoryGraphEdge;
using cpipe::runtime::MemoryPlanner;

constexpr char kRgba8Manifest[] = R"({
  "id":"com.cpipe.test.memory",
  "version":"1.0.0",
  "ports":[
    {"name":"in","kind":"in","caps":{"channels":["rgba"],"precision":["u8"]}},
    {"name":"out","kind":"out","caps":{"channels":["rgba"],"precision":["u8"]}}
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":4},
  "color":{"input_role":"any","output_role":"any"}
})";

const cpipe_plugin_desc_t kNode{
    .abi_major = CPIPE_ABI_MAJOR,
    .abi_minor = CPIPE_ABI_MINOR,
    .node_id = "com.cpipe.test.memory",
    .node_version = "1.0.0",
    .manifest_json = kRgba8Manifest,
    .main_entry = nullptr,
};

BufferLayout rgba8_layout() {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 64;
    layout.dims[1] = 64;
    return layout;
}

std::size_t slot_for_node(const cpipe::runtime::MemoryPlan& plan, std::size_t node_index) {
    for (const auto& assignment : plan.assignments) {
        if (assignment.node_index == node_index) {
            return assignment.physical_slot;
        }
    }
    FAIL("missing memory assignment for node " << node_index);
    return 0;
}

}  // namespace

TEST_CASE("MemoryPlanner keeps P1 linear pipeline allocation count") {
    const cpipe_plugin_desc_t* nodes[] = {&kNode, &kNode, &kNode};

    const auto plan = MemoryPlanner::plan(rgba8_layout(), nodes);

    REQUIRE(plan.physical_slot_count == 1);
    REQUIRE(plan.assignments.size() == 3);
    REQUIRE(plan.peak_bytes == rgba8_layout().size_bytes() * 2);
}

TEST_CASE("MemoryPlanner colors fan-in graph into two physical slots") {
    const cpipe_plugin_desc_t* nodes[] = {&kNode, &kNode, &kNode};
    const std::vector<MemoryGraphEdge> edges{
        {.from = 0, .to = 2},
        {.from = 1, .to = 2},
    };

    const auto plan = MemoryPlanner::plan_graph(rgba8_layout(), nodes, edges);

    REQUIRE(plan.physical_slot_count == 2);
    REQUIRE(slot_for_node(plan, 0) != slot_for_node(plan, 1));
}

TEST_CASE("MemoryPlanner colors fan-out graph into two physical slots") {
    const cpipe_plugin_desc_t* nodes[] = {&kNode, &kNode, &kNode, &kNode};
    const std::vector<MemoryGraphEdge> edges{
        {.from = 0, .to = 1},
        {.from = 0, .to = 2},
        {.from = 1, .to = 3},
        {.from = 2, .to = 3},
    };

    const auto plan = MemoryPlanner::plan_graph(rgba8_layout(), nodes, edges);

    REQUIRE(plan.physical_slot_count == 2);
    REQUIRE(slot_for_node(plan, 1) != slot_for_node(plan, 2));
}

TEST_CASE("MemoryPlanner reuses diamond source slot for sink output") {
    const cpipe_plugin_desc_t* nodes[] = {&kNode, &kNode, &kNode, &kNode, &kNode};
    const std::vector<MemoryGraphEdge> edges{
        {.from = 0, .to = 1}, {.from = 0, .to = 2}, {.from = 1, .to = 3},
        {.from = 2, .to = 3}, {.from = 3, .to = 4},
    };

    const auto plan = MemoryPlanner::plan_graph(rgba8_layout(), nodes, edges);

    REQUIRE(plan.physical_slot_count == 2);
    REQUIRE(slot_for_node(plan, 0) == slot_for_node(plan, 4));
}
