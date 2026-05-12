// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/Scheduler.hpp"

#include "cpipe/runtime/Registry.hpp"

namespace cpipe::runtime {

Scheduler::Scheduler(TaskExecutor& executor) noexcept : executor_(&executor) {}

auto Scheduler::run(std::span<const ScheduledNode> topo_order, ComputeContext& compute,
                    InferenceContext& inference) -> int {
    if (executor_ == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }

    auto host = make_host();
    for (const auto& scheduled : topo_order) {
        if (scheduled.main_entry == nullptr) {
            return CPIPE_BAD_INDEX;
        }

        auto status = static_cast<int>(CPIPE_INTERNAL_ERROR);
        cpipe_process_ctx process_ctx{compute.native(),         inference.native(),
                                      scheduled.inputs.data(),  scheduled.inputs.size(),
                                      scheduled.outputs.data(), scheduled.outputs.size()};
        executor_->run([&]() {
            status = scheduled.main_entry(CPIPE_ACTION_PROCESS, &host, scheduled.node,
                                          scheduled.params, &process_ctx, scheduled.out_ctx);
        });
        if (status != CPIPE_OK) {
            return status;
        }
    }

    return CPIPE_OK;
}

}  // namespace cpipe::runtime
