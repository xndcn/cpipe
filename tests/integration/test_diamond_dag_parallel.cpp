// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

TEST_CASE("Scheduler runs independent diamond DAG interior nodes concurrently") {
    using namespace std::chrono_literals;

    std::atomic<int> active_mid_nodes{0};
    std::atomic<int> max_active_mid_nodes{0};
    cpipe::runtime::Scheduler scheduler{2};

    auto mid_node = [&] {
        const int active = active_mid_nodes.fetch_add(1) + 1;
        int observed = max_active_mid_nodes.load();
        while (active > observed &&
               !max_active_mid_nodes.compare_exchange_weak(observed, active)) {
        }
        std::this_thread::sleep_for(50ms);
        active_mid_nodes.fetch_sub(1);
        return CPIPE_OK;
    };

    const std::vector<cpipe::runtime::ScheduledNode> nodes{
        {.id = "source", .process = [] { return CPIPE_OK; }, .dependencies = {}},
        {.id = "mid_a", .process = mid_node, .dependencies = {0}},
        {.id = "mid_b", .process = mid_node, .dependencies = {0}},
        {.id = "sink", .process = [] { return CPIPE_OK; }, .dependencies = {1, 2}},
    };

    REQUIRE(scheduler.run(nodes) == CPIPE_OK);
    REQUIRE(max_active_mid_nodes.load() >= 2);
}
