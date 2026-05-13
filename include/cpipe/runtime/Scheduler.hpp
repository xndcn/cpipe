// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <taskflow/taskflow.hpp>

namespace cpipe::runtime {

struct ScheduledNode {
    std::string_view id;
    std::function<cpipe_status_t()> process;
};

class Scheduler {
public:
    explicit Scheduler(std::size_t worker_count = default_worker_count());

    [[nodiscard]] cpipe_status_t run(std::span<const ScheduledNode> nodes);
    [[nodiscard]] tf::Executor& executor() noexcept;

    static std::size_t default_worker_count() noexcept;

private:
    tf::Executor executor_;
};

}  // namespace cpipe::runtime
