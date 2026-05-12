// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace cpipe::runtime {

class TaskExecutor final {
public:
    struct Impl;

    TaskExecutor();
    explicit TaskExecutor(std::size_t worker_count);
    ~TaskExecutor();

    TaskExecutor(const TaskExecutor&) = delete;
    auto operator=(const TaskExecutor&) -> TaskExecutor& = delete;

    TaskExecutor(TaskExecutor&&) = delete;
    auto operator=(TaskExecutor&&) -> TaskExecutor& = delete;

    void run(std::function<void()> task);

    [[nodiscard]] auto worker_count() const noexcept -> std::size_t;
    [[nodiscard]] auto halide_tasks_dispatched() const noexcept -> std::uint64_t;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace cpipe::runtime
