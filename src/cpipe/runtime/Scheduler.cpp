// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <atomic>
#include <cpipe/runtime/Scheduler.hpp>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace cpipe::runtime {
namespace {

std::mutex g_halide_executor_mutex;
tf::Executor* g_halide_executor = nullptr;
std::vector<tf::Executor*> g_halide_executor_stack;
halide_do_par_for_t g_previous_halide_do_par_for = nullptr;
bool g_halide_custom_installed = false;

[[nodiscard]] std::size_t default_worker_count() {
    const auto hw_threads = std::thread::hardware_concurrency();
    return static_cast<std::size_t>(std::max(1U, hw_threads > 1U ? hw_threads - 1U : 1U));
}

int run_halide_tasks_serial(void* user_context, halide_task_t task, int min, int extent,
                            std::uint8_t* closure) {
    for (int offset = 0; offset < extent; ++offset) {
        const auto status = task(user_context, min + offset, closure);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

int taskflow_do_par_for(void* user_context, halide_task_t task, int min, int extent,
                        std::uint8_t* closure) {
    if (task == nullptr || extent <= 0) {
        return 0;
    }

    tf::Executor* executor = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_halide_executor_mutex);
        executor = g_halide_executor;
    }

    if (executor == nullptr || executor->num_workers() == 0 || extent == 1) {
        return run_halide_tasks_serial(user_context, task, min, extent, closure);
    }

    std::atomic<int> result{0};
    tf::Taskflow taskflow;
    constexpr int chunk_size = 1024;

    for (int chunk_begin = 0; chunk_begin < extent; chunk_begin += chunk_size) {
        const auto chunk_end = std::min(chunk_begin + chunk_size, extent);
        taskflow.emplace([user_context, task, min, chunk_begin, chunk_end, closure, &result] {
            for (int offset = chunk_begin; offset < chunk_end; ++offset) {
                if (result.load(std::memory_order_acquire) != 0) {
                    return;
                }
                const auto task_status = task(user_context, min + offset, closure);
                if (task_status == 0) {
                    continue;
                }
                int expected = 0;
                static_cast<void>(result.compare_exchange_strong(expected, task_status,
                                                                 std::memory_order_acq_rel));
                return;
            }
        });
    }

    if (executor->this_worker() == nullptr) {
        executor->run(taskflow).wait();
    } else {
        executor->corun(taskflow);
    }
    return result.load(std::memory_order_acquire);
}

}  // namespace

Scheduler::Scheduler() : Scheduler(default_worker_count()) {}

Scheduler::Scheduler(std::size_t worker_count)
    : executor_(worker_count), worker_count_(worker_count) {
    const std::lock_guard<std::mutex> lock(g_halide_executor_mutex);
    if (!g_halide_custom_installed) {
        g_previous_halide_do_par_for = halide_set_custom_do_par_for(&taskflow_do_par_for);
        g_halide_custom_installed = true;
    }
    g_halide_executor_stack.push_back(&executor_);
    g_halide_executor = &executor_;
    halide_parallelism_installed_ = true;
}

Scheduler::~Scheduler() {
    const std::lock_guard<std::mutex> lock(g_halide_executor_mutex);
    std::erase(g_halide_executor_stack, &executor_);
    if (!g_halide_executor_stack.empty()) {
        g_halide_executor = g_halide_executor_stack.back();
        return;
    }

    if (g_halide_custom_installed) {
        static_cast<void>(halide_set_custom_do_par_for(g_previous_halide_do_par_for));
        g_previous_halide_do_par_for = nullptr;
        g_halide_custom_installed = false;
        g_halide_executor = nullptr;
    }
}

cpipe_status_t Scheduler::run_serial(std::span<const ScheduledNode> nodes) const {
    for (const auto& node : nodes) {
        if (!node.process) {
            return CPIPE_INTERNAL_ERROR;
        }
        const auto status = node.process();
        if (status != CPIPE_OK) {
            return status;
        }
    }
    return CPIPE_OK;
}

std::size_t Scheduler::worker_count() const noexcept {
    return worker_count_;
}

bool Scheduler::halide_parallelism_installed() const noexcept {
    return halide_parallelism_installed_;
}

}  // namespace cpipe::runtime
