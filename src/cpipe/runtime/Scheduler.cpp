// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <algorithm>
#include <atomic>
#include <cpipe/runtime/Scheduler.hpp>
#include <thread>

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

cpipe_status_t Scheduler::run(std::span<const ScheduledNode> nodes) {
    for (const auto& node : nodes) {
        (void)node.id;
        const auto status = node.process();
        if (status != CPIPE_OK) {
            return status;
        }
    }
    return CPIPE_OK;
}

tf::Executor& Scheduler::executor() noexcept {
    return executor_;
}

std::size_t Scheduler::default_worker_count() noexcept {
    const auto hardware = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, static_cast<std::size_t>(hardware > 0 ? hardware - 1 : 1));
}

}  // namespace cpipe::runtime
