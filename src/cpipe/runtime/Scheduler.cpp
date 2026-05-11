// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/runtime/Scheduler.hpp>
#include <thread>

namespace cpipe::runtime {
namespace {

[[nodiscard]] std::size_t default_worker_count() {
    const auto hw_threads = std::thread::hardware_concurrency();
    return static_cast<std::size_t>(std::max(1U, hw_threads > 1U ? hw_threads - 1U : 1U));
}

}  // namespace

Scheduler::Scheduler() : Scheduler(default_worker_count()) {}

Scheduler::Scheduler(std::size_t worker_count)
    : executor_(worker_count), worker_count_(worker_count) {
    halide_parallelism_installed_ = true;
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
