// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Scheduler.hpp>
#include <string>
#include <vector>

namespace {

halide_do_par_for_t g_halide_par_for = nullptr;

int count_halide_task(void*, int, std::uint8_t* closure) {
    auto* counter = reinterpret_cast<std::atomic<int>*>(closure);
    counter->fetch_add(1);
    return 0;
}

}  // namespace

extern "C" halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t do_par_for) {
    const auto old = g_halide_par_for;
    g_halide_par_for = do_par_for;
    return old;
}

TEST_CASE("Scheduler dispatches already-topologically-sorted nodes serially") {
    std::vector<std::string> order;
    cpipe::runtime::Scheduler scheduler{1};

    const std::vector<cpipe::runtime::ScheduledNode> nodes{
        {.id = "input",
         .process =
             [&order] {
                 order.emplace_back("input");
                 return CPIPE_OK;
             }},
        {.id = "passthrough",
         .process =
             [&order] {
                 order.emplace_back("passthrough");
                 return CPIPE_OK;
             }},
        {.id = "output",
         .process =
             [&order] {
                 order.emplace_back("output");
                 return CPIPE_OK;
             }},
    };

    REQUIRE(scheduler.run(nodes) == CPIPE_OK);
    REQUIRE(order == std::vector<std::string>{"input", "passthrough", "output"});
}

TEST_CASE("Scheduler installs Halide custom do_par_for hook") {
    cpipe::runtime::Scheduler scheduler{2};
    REQUIRE(g_halide_par_for != nullptr);

    std::atomic<int> counter{0};
    REQUIRE(g_halide_par_for(nullptr, &count_halide_task, 0, 4,
                             reinterpret_cast<std::uint8_t*>(&counter)) == 0);
    REQUIRE(counter.load() == 4);
}

TEST_CASE("Scheduler stops at the first failing node") {
    std::vector<std::string> order;
    cpipe::runtime::Scheduler scheduler{1};

    const std::vector<cpipe::runtime::ScheduledNode> nodes{
        {.id = "first",
         .process =
             [&order] {
                 order.emplace_back("first");
                 return CPIPE_OK;
             }},
        {.id = "bad",
         .process =
             [&order] {
                 order.emplace_back("bad");
                 return CPIPE_FAILED;
             }},
        {.id = "after_bad",
         .process =
             [&order] {
                 order.emplace_back("after_bad");
                 return CPIPE_OK;
             }},
    };

    REQUIRE(scheduler.run(nodes) == CPIPE_FAILED);
    REQUIRE(order == std::vector<std::string>{"first", "bad"});
}
