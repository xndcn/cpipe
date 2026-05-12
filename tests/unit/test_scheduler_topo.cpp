// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string_view>
#include <vector>

#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/Scheduler.hpp"
#include "cpipe/runtime/TaskExecutor.hpp"
#include "cpipe/sdk/cpipe_node.h"

namespace {
struct ProcessProbe {
    std::string_view label;
    std::vector<std::string_view>* order = nullptr;
    bool saw_compute = false;
    bool saw_inference = false;
};

auto record_process(const char* action, cpipe_host_t* host, cpipe_node_t* node,
                    cpipe_props_t* params, void* in_ctx, void* out_ctx) -> int {
    (void)host;
    (void)node;
    (void)params;

    if (std::strcmp(action, CPIPE_ACTION_PROCESS) != 0 || in_ctx == nullptr || out_ctx == nullptr) {
        return CPIPE_FAILED;
    }

    auto* process_ctx = static_cast<cpipe_process_ctx*>(in_ctx);
    auto* probe = static_cast<ProcessProbe*>(out_ctx);
    if (process_ctx->compute == nullptr || process_ctx->inference == nullptr ||
        probe->order == nullptr) {
        return CPIPE_FAILED;
    }

    probe->saw_compute = true;
    probe->saw_inference = true;
    probe->order->push_back(probe->label);
    return CPIPE_OK;
}
}  // namespace

TEST_CASE("Scheduler walks a topologically sorted node list in order") {
    cpipe::runtime::TaskExecutor executor{2};
    cpipe::runtime::ComputeContext compute{executor, {}};
    cpipe::runtime::InferenceContext inference;
    cpipe::runtime::Scheduler scheduler{executor};

    std::vector<std::string_view> order;
    ProcessProbe raw{"raw", &order};
    ProcessProbe tone{"tone", &order};
    ProcessProbe output{"output", &order};

    std::array<cpipe::runtime::ScheduledNode, 3> nodes{};
    nodes[0].main_entry = &record_process;
    nodes[0].out_ctx = &raw;
    nodes[1].main_entry = &record_process;
    nodes[1].out_ctx = &tone;
    nodes[2].main_entry = &record_process;
    nodes[2].out_ctx = &output;

    CHECK(scheduler.run(nodes, compute, inference) == CPIPE_OK);

    REQUIRE(order.size() == 3U);
    CHECK(order[0] == "raw");
    CHECK(order[1] == "tone");
    CHECK(order[2] == "output");
    CHECK(raw.saw_compute);
    CHECK(tone.saw_inference);
    CHECK(output.saw_compute);
}
