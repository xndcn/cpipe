// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <taskflow/taskflow.hpp>

namespace cpipe::runtime {

struct ScheduledNode {
    std::string id;
    std::function<cpipe_status_t()> process;
};

class Scheduler {
public:
    Scheduler();

    [[nodiscard]] cpipe_status_t run_serial(std::span<const ScheduledNode> nodes) const;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] bool halide_parallelism_installed() const noexcept;

private:
    explicit Scheduler(std::size_t worker_count);

    tf::Executor executor_;
    std::size_t worker_count_ = 1;
    bool halide_parallelism_installed_ = false;
};

}  // namespace cpipe::runtime
