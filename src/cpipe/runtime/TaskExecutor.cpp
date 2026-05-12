// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/TaskExecutor.hpp"

#include <HalideRuntime.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <utility>

namespace cpipe::runtime {

struct TaskExecutor::Impl {
    explicit Impl(std::size_t worker_count_in)
        : executor(worker_count_in), worker_count(worker_count_in) {}

    tf::Executor executor;
    std::size_t worker_count = 1;
    std::atomic<std::uint64_t> halide_tasks_dispatched{0};
};

namespace {

// Halide's custom CPU parallel hook is process-global.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex g_halide_hook_mutex;
TaskExecutor::Impl* g_halide_executor = nullptr;
halide_do_par_for_t g_previous_do_par_for = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] auto default_worker_count() noexcept -> std::size_t {
    const auto hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= 1U) {
        return 1U;
    }
    return static_cast<std::size_t>(hardware_threads - 1U);
}

auto taskflow_do_par_for(void* user_context, halide_task_t task, int min, int size,
                         std::uint8_t* closure) -> int {
    TaskExecutor::Impl* impl = nullptr;
    halide_do_par_for_t fallback = nullptr;
    {
        std::lock_guard<std::mutex> lock{g_halide_hook_mutex};
        impl = g_halide_executor;
        fallback = g_previous_do_par_for;
    }

    if (impl == nullptr) {
        return fallback == nullptr
                   ? halide_default_do_par_for(user_context, task, min, size, closure)
                   : fallback(user_context, task, min, size, closure);
    }
    if (task == nullptr || size <= 0) {
        return 0;
    }

    std::atomic<int> first_error{0};
    tf::Taskflow flow;
    for (int offset = 0; offset < size; ++offset) {
        flow.emplace([=, &first_error]() {
            const auto result = task(user_context, min + offset, closure);
            impl->halide_tasks_dispatched.fetch_add(1U, std::memory_order_relaxed);
            if (result != 0) {
                int expected = 0;
                (void)first_error.compare_exchange_strong(expected, result,
                                                          std::memory_order_relaxed);
            }
        });
    }
    if (impl->executor.this_worker() != nullptr) {
        impl->executor.corun(flow);
    } else {
        impl->executor.run(flow).wait();
    }
    return first_error.load(std::memory_order_relaxed);
}

void install_halide_hook(TaskExecutor::Impl* impl) {
    std::lock_guard<std::mutex> lock{g_halide_hook_mutex};
    if (g_halide_executor == nullptr) {
        g_previous_do_par_for = halide_set_custom_do_par_for(&taskflow_do_par_for);
    }
    g_halide_executor = impl;
}

void uninstall_halide_hook(TaskExecutor::Impl* impl) {
    std::lock_guard<std::mutex> lock{g_halide_hook_mutex};
    if (g_halide_executor == impl) {
        g_halide_executor = nullptr;
        (void)halide_set_custom_do_par_for(g_previous_do_par_for);
        g_previous_do_par_for = nullptr;
    }
}

}  // namespace

TaskExecutor::TaskExecutor() : TaskExecutor(default_worker_count()) {}

TaskExecutor::TaskExecutor(std::size_t worker_count)
    : impl_(std::make_unique<Impl>(std::max<std::size_t>(1U, worker_count))) {
    install_halide_hook(impl_.get());
}

TaskExecutor::~TaskExecutor() {
    uninstall_halide_hook(impl_.get());
}

void TaskExecutor::run(std::function<void()> task) {
    tf::Taskflow flow;
    flow.emplace(std::move(task));
    if (impl_->executor.this_worker() != nullptr) {
        impl_->executor.corun(flow);
    } else {
        impl_->executor.run(flow).wait();
    }
}

auto TaskExecutor::worker_count() const noexcept -> std::size_t {
    return impl_->worker_count;
}

auto TaskExecutor::halide_tasks_dispatched() const noexcept -> std::uint64_t {
    return impl_->halide_tasks_dispatched.load(std::memory_order_relaxed);
}

}  // namespace cpipe::runtime
