// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/Scheduler.hpp"

namespace cpipe::runtime {

Result<void> Scheduler::run(const std::vector<ScheduleStep>& steps) const {
    for (const auto& step : steps) {
        const StatusCode status = step.process();
        if (status != StatusCode::Ok) {
            return tl::unexpected(make_error(status, "scheduler step failed: " + step.id));
        }
    }
    return {};
}

}  // namespace cpipe::runtime
