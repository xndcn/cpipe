// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <algorithm>
#include <atomic>
#include <cpipe/runtime/Scheduler.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <thread>
#include <vector>

extern "C" {
#pragma weak halide_set_custom_do_par_for
}

namespace {

std::atomic<tf::Executor*> g_halide_executor{nullptr};

int cpipe_halide_do_par_for(void* user_context, halide_task_t task, int min, int size,
                            std::uint8_t* closure) {
    auto* executor = g_halide_executor.load();
    if (executor == nullptr || size <= 1) {
        for (int i = min; i < min + size; ++i) {
            const int status = task(user_context, i, closure);
            if (status != 0) {
                return status;
            }
        }
        return 0;
    }

    std::atomic<int> first_error{0};
    tf::Taskflow taskflow;
    for (int i = min; i < min + size; ++i) {
        taskflow.emplace([=, &first_error] {
            const int status = task(user_context, i, closure);
            int expected = 0;
            if (status != 0) {
                first_error.compare_exchange_strong(expected, status);
            }
        });
    }
    executor->run(taskflow).wait();
    return first_error.load();
}

}  // namespace

namespace cpipe::runtime {

Scheduler::Scheduler(std::size_t worker_count) : executor_(worker_count) {
    g_halide_executor.store(&executor_);
    if (halide_set_custom_do_par_for != nullptr) {
        halide_set_custom_do_par_for(&cpipe_halide_do_par_for);
    }
}

Scheduler::~Scheduler() {
    auto* expected = &executor_;
    (void)g_halide_executor.compare_exchange_strong(expected, nullptr);
}

cpipe_status_t Scheduler::run(std::span<const ScheduledNode> nodes) {
    const bool has_dependencies = std::any_of(
        nodes.begin(), nodes.end(), [](const auto& node) { return !node.dependencies.empty(); });
    if (!has_dependencies) {
        for (const auto& node : nodes) {
            (void)node.id;
            CPIPE_TRACE_SCOPE("Scheduler::dispatch_node");
            const auto status = node.process();
            if (status != CPIPE_OK) {
                return status;
            }
        }
        return CPIPE_OK;
    }

    tf::Taskflow taskflow;
    std::vector<tf::Task> tasks;
    tasks.reserve(nodes.size());
    std::atomic<int> first_status{CPIPE_OK};

    for (const auto& node : nodes) {
        tasks.push_back(taskflow.emplace([&node, &first_status] {
            if (first_status.load() != CPIPE_OK) {
                return;
            }
            CPIPE_TRACE_SCOPE("Scheduler::dispatch_node");
            const auto status = node.process();
            int expected = CPIPE_OK;
            if (status != CPIPE_OK) {
                first_status.compare_exchange_strong(expected, status);
            }
        }));
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (const auto dependency : nodes[i].dependencies) {
            if (dependency >= tasks.size()) {
                return CPIPE_BAD_INDEX;
            }
            tasks[dependency].precede(tasks[i]);
        }
    }

    executor_.run(taskflow).wait();
    return static_cast<cpipe_status_t>(first_status.load());
}

tf::Executor& Scheduler::executor() noexcept {
    return executor_;
}

std::size_t Scheduler::default_worker_count() noexcept {
    const auto hardware = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, static_cast<std::size_t>(hardware > 0 ? hardware - 1 : 1));
}

}  // namespace cpipe::runtime
