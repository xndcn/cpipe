// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/InferenceContext.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <span>

namespace cpipe::runtime {

struct ScheduledNode {
    const cpipe_plugin_desc_t* descriptor = nullptr;
    cpipe_node_t* node = nullptr;
    cpipe_props_t* params = nullptr;
    cpipe_process_ctx* process = nullptr;
};

class Scheduler {
public:
    Scheduler();

    [[nodiscard]] auto compute_handle() noexcept -> cpipe_compute_t*;
    [[nodiscard]] auto inference_handle() noexcept -> cpipe_inference_t*;
    [[nodiscard]] auto run(std::span<const ScheduledNode> nodes) -> int;

private:
    ComputeContext compute_;
    InferenceContext inference_;
    cpipe_host_t host_{};
};

}  // namespace cpipe::runtime
