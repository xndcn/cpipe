// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/ComputeContext.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "RuntimeHandles.hpp"
#include "cpipe/runtime/HalideBufferAdapter.hpp"

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output)
    __attribute__((weak));

namespace cpipe::runtime {
namespace {

tf::Executor* g_halide_executor = nullptr;

int taskflow_do_par_for(void* user_context, halide_task_t task, int min, int size,
                        uint8_t* closure) {
    if (g_halide_executor == nullptr || task == nullptr) {
        return halide_default_do_par_for(user_context, task, min, size, closure);
    }

    std::vector<std::future<int>> futures;
    futures.reserve(static_cast<std::size_t>(size));
    for (int index = min; index < min + size; ++index) {
        futures.push_back(
            g_halide_executor->async([=]() { return task(user_context, index, closure); }));
    }

    int status = 0;
    for (auto& future : futures) {
        const int task_status = future.get();
        if (task_status != 0 && status == 0) {
            status = task_status;
        }
    }
    return status;
}

std::size_t worker_count() noexcept {
    const auto hardware = std::thread::hardware_concurrency();
    return hardware > 1u ? static_cast<std::size_t>(hardware - 1u) : 1u;
}

}  // namespace

ComputeContext::ComputeContext() : executor_(worker_count()) {
    g_halide_executor = &executor_;
    static_cast<void>(halide_set_custom_do_par_for(taskflow_do_par_for));
    if (passthrough_copy != nullptr) {
        halide_filters_.emplace("passthrough_copy", passthrough_copy);
    }
}

tf::Executor& ComputeContext::executor() noexcept {
    return executor_;
}

int ComputeContext::submit_halide(std::string_view aot_id, const cpipe_buffer_t* const* inputs,
                                  std::size_t n_in, cpipe_buffer_t* const* outputs,
                                  std::size_t n_out) {
    if (n_in != 1 || n_out != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto it = halide_filters_.find(aot_id);
    if (it == halide_filters_.end()) {
        spdlog::warn("Unknown Halide AOT id: {}", aot_id);
        return CPIPE_UNSUPPORTED;
    }

    auto in_adapter = HalideBufferAdapter::from_buffer(*inputs[0]->buffer);
    if (!in_adapter) {
        return CPIPE_UNSUPPORTED;
    }
    auto out_adapter = HalideBufferAdapter::from_buffer(*outputs[0]->buffer);
    if (!out_adapter) {
        return CPIPE_UNSUPPORTED;
    }

    const int status = it->second(in_adapter->get(), out_adapter->get());
    return status == 0 ? CPIPE_OK : CPIPE_FAILED;
}

}  // namespace cpipe::runtime
