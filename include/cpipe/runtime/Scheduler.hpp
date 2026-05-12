// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "cpipe/core/Status.hpp"

namespace cpipe::runtime {

struct ScheduleStep {
    std::string id;
    std::function<StatusCode()> process;
};

class Scheduler {
public:
    [[nodiscard]] Result<void> run(const std::vector<ScheduleStep>& steps) const;
};

}  // namespace cpipe::runtime
