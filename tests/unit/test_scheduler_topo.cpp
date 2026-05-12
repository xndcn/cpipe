// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Scheduler.hpp>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string_view>* g_calls = nullptr;

auto first_entry(const char*, cpipe_host_t*, cpipe_node_t*, cpipe_props_t*, void*, void*) -> int {
    g_calls->push_back("first");
    return CPIPE_OK;
}

auto second_entry(const char*, cpipe_host_t*, cpipe_node_t*, cpipe_props_t*, void*, void*) -> int {
    g_calls->push_back("second");
    return CPIPE_OK;
}

auto failing_entry(const char*, cpipe_host_t*, cpipe_node_t*, cpipe_props_t*, void*, void*) -> int {
    g_calls->push_back("failing");
    return CPIPE_FAILED;
}

const cpipe_plugin_desc_t kFirstDescriptor{CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, "first", "1.0.0", "{}",
                                           &first_entry};
const cpipe_plugin_desc_t kSecondDescriptor{
    CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, "second", "1.0.0", "{}", &second_entry};
const cpipe_plugin_desc_t kFailingDescriptor{
    CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, "failing", "1.0.0", "{}", &failing_entry};

}  // namespace

TEST_CASE("Scheduler walks topologically sorted nodes in order") {
    auto calls = std::vector<std::string_view>{};
    g_calls = &calls;

    auto scheduler = cpipe::runtime::Scheduler{};
    cpipe_process_ctx first_context{};
    cpipe_process_ctx second_context{};
    const auto nodes = std::vector<cpipe::runtime::ScheduledNode>{
        cpipe::runtime::ScheduledNode{&kFirstDescriptor, nullptr, nullptr, &first_context},
        cpipe::runtime::ScheduledNode{&kSecondDescriptor, nullptr, nullptr, &second_context},
    };

    CHECK(scheduler.run(nodes) == CPIPE_OK);
    CHECK(calls == std::vector<std::string_view>{"first", "second"});
    CHECK(first_context.compute == scheduler.compute_handle());
    CHECK(second_context.compute == scheduler.compute_handle());
}

TEST_CASE("Scheduler stops at the first failing node") {
    auto calls = std::vector<std::string_view>{};
    g_calls = &calls;

    auto scheduler = cpipe::runtime::Scheduler{};
    cpipe_process_ctx first_context{};
    cpipe_process_ctx failing_context{};
    cpipe_process_ctx second_context{};
    const auto nodes = std::vector<cpipe::runtime::ScheduledNode>{
        cpipe::runtime::ScheduledNode{&kFirstDescriptor, nullptr, nullptr, &first_context},
        cpipe::runtime::ScheduledNode{&kFailingDescriptor, nullptr, nullptr, &failing_context},
        cpipe::runtime::ScheduledNode{&kSecondDescriptor, nullptr, nullptr, &second_context},
    };

    CHECK(scheduler.run(nodes) == CPIPE_FAILED);
    CHECK(calls == std::vector<std::string_view>{"first", "failing"});
}
