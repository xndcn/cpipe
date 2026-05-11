// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Scheduler.hpp>

#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("test_scheduler_topo: dispatches nodes in supplied topological order") {
    cpipe::runtime::Scheduler scheduler;
    std::vector<int> visited;

    const std::vector<cpipe::runtime::ScheduledNode> nodes{
        {.id = "a", .process = [&visited] {
             visited.push_back(1);
             return CPIPE_OK;
         }},
        {.id = "b", .process = [&visited] {
             visited.push_back(2);
             return CPIPE_OK;
         }},
        {.id = "c", .process = [&visited] {
             visited.push_back(3);
             return CPIPE_OK;
         }},
    };

    CHECK(scheduler.run_serial(nodes) == CPIPE_OK);
    CHECK(visited == std::vector<int>{1, 2, 3});
    CHECK(scheduler.worker_count() >= 1);
}

TEST_CASE("test_scheduler_topo: stops on first node failure") {
    cpipe::runtime::Scheduler scheduler;
    std::vector<int> visited;

    const std::vector<cpipe::runtime::ScheduledNode> nodes{
        {.id = "a", .process = [&visited] {
             visited.push_back(1);
             return CPIPE_OK;
         }},
        {.id = "b", .process = [&visited] {
             visited.push_back(2);
             return CPIPE_FAILED;
         }},
        {.id = "c", .process = [&visited] {
             visited.push_back(3);
             return CPIPE_OK;
         }},
    };

    CHECK(scheduler.run_serial(nodes) == CPIPE_FAILED);
    CHECK(visited == std::vector<int>{1, 2});
}
