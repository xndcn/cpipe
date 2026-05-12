// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <span>

#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/TaskExecutor.hpp"
#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

struct ScheduledNode {
    cpipe_main_entry_t main_entry = nullptr;
    cpipe_node_t* node = nullptr;
    cpipe_props_t* params = nullptr;
    std::span<const cpipe_buffer_t*> inputs;
    std::span<cpipe_buffer_t*> outputs;
    void* out_ctx = nullptr;
};

class Scheduler final {
public:
    explicit Scheduler(TaskExecutor& executor) noexcept;

    [[nodiscard]] auto run(std::span<const ScheduledNode> topo_order, ComputeContext& compute,
                           InferenceContext& inference) -> int;

private:
    TaskExecutor* executor_ = nullptr;
};

}  // namespace cpipe::runtime
