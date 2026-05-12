// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Scheduler.hpp>

namespace cpipe::runtime {

Scheduler::Scheduler() : host_(make_default_host()) {}

auto Scheduler::compute_handle() noexcept -> cpipe_compute_t* {
    return compute_.handle();
}

auto Scheduler::inference_handle() noexcept -> cpipe_inference_t* {
    return inference_.handle();
}

auto Scheduler::run(std::span<const ScheduledNode> nodes) -> int {
    for (const auto& scheduled : nodes) {
        if (scheduled.descriptor == nullptr || scheduled.descriptor->main_entry == nullptr) {
            return CPIPE_INTERNAL_ERROR;
        }

        auto fallback_process = cpipe_process_ctx{};
        auto* process = scheduled.process == nullptr ? &fallback_process : scheduled.process;
        process->compute = compute_.handle();
        process->inference = inference_.handle();

        const auto status = scheduled.descriptor->main_entry(
            CPIPE_ACTION_PROCESS, &host_, scheduled.node, scheduled.params, process, nullptr);
        if (status != CPIPE_OK) {
            return status;
        }
    }
    return CPIPE_OK;
}

}  // namespace cpipe::runtime
