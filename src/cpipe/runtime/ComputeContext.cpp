// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <atomic>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cpipe::runtime {
namespace {

inline constexpr auto kMinimumWorkerCount = std::size_t{1};
inline constexpr int kMaxCpuAccessValue = 2;

std::atomic<tf::Executor*> g_halide_executor{nullptr};      // NOLINT
std::atomic<std::uint64_t> g_halide_parallel_for_calls{0};  // NOLINT

[[nodiscard]] auto default_worker_count() noexcept -> std::size_t {
    const auto hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= kMinimumWorkerCount) {
        return kMinimumWorkerCount;
    }
    return static_cast<std::size_t>(hardware_threads - kMinimumWorkerCount);
}

[[nodiscard]] auto buffer_from_handle(cpipe_buffer_t* handle) noexcept -> compute::IBuffer* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<compute::IBuffer*>(handle);
}

[[nodiscard]] auto buffer_from_handle(const cpipe_buffer_t* handle) noexcept
    -> const compute::IBuffer* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const compute::IBuffer*>(handle);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto taskflow_do_par_for(void* user_context, halide_task_t task, int min, int size,
                         std::uint8_t* closure) -> int {
    ++g_halide_parallel_for_calls;
    if (task == nullptr || size <= 0) {
        return 0;
    }

    auto* executor = g_halide_executor.load(std::memory_order_acquire);
    if (executor == nullptr || size == 1) {
        auto status = 0;
        for (auto index = 0; index < size; ++index) {
            const auto next = task(user_context, min + index, closure);
            if (next != 0 && status == 0) {
                status = next;
            }
        }
        return status;
    }

    auto status = std::atomic<int>{0};
    auto taskflow = tf::Taskflow{};
    for (auto index = 0; index < size; ++index) {
        taskflow.emplace([user_context, task, min, index, closure, &status] {
            if (status.load(std::memory_order_relaxed) != 0) {
                return;
            }
            const auto next = task(user_context, min + index, closure);
            auto expected = 0;
            if (next != 0) {
                static_cast<void>(
                    status.compare_exchange_strong(expected, next, std::memory_order_relaxed));
            }
        });
    }
    executor->run(taskflow).wait();
    return status.load(std::memory_order_relaxed);
}

// NOLINTBEGIN(readability-named-parameter,bugprone-easily-swappable-parameters)
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

auto get_dims(const cpipe_buffer_t* buffer, std::uint8_t* ndim,
              std::uint32_t out_dims[CPIPE_MAX_BUFFER_DIMS]) -> int {
    if (buffer == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto& layout = owner->layout();
    *ndim = layout.ndim;
    for (std::uint8_t index = 0; index < compute::kMaxBufferDimensions; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        out_dims[index] = layout.dims[index];
    }
    return CPIPE_OK;
}

auto get_format(const cpipe_buffer_t* buffer, int* out) -> int {
    if (buffer == nullptr || out == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    *out = static_cast<int>(owner->layout().format);
    return CPIPE_OK;
}

auto get_kind(const cpipe_buffer_t* buffer, int* out) -> int {
    if (buffer == nullptr || out == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    *out = static_cast<int>(owner->layout().kind);
    return CPIPE_OK;
}

auto get_stride(const cpipe_buffer_t* buffer, std::uint64_t out_stride[CPIPE_MAX_BUFFER_DIMS])
    -> int {
    if (buffer == nullptr || out_stride == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto& layout = owner->layout();
    for (std::uint8_t index = 0; index < compute::kMaxBufferDimensions; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        out_stride[index] = layout.stride[index];
    }
    return CPIPE_OK;
}

auto get_color_role(const cpipe_buffer_t* buffer, const char** out) -> int {
    if (buffer == nullptr || out == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto role = owner->color_role();
    *out = role.empty() ? "" : role.data();
    return CPIPE_OK;
}

auto lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr) -> int {
    if (buffer == nullptr || ptr == nullptr || access < 0 || access > kMaxCpuAccessValue) {
        return CPIPE_INTERNAL_ERROR;
    }
    auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    const auto cpu_access = static_cast<compute::IBuffer::CpuAccess>(access);
    *ptr = owner->lock_cpu(cpu_access);
    return *ptr == nullptr ? CPIPE_FAILED : CPIPE_OK;
}

auto unlock_cpu(cpipe_buffer_t* buffer) -> int {
    if (buffer == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    owner->unlock_cpu();
    return CPIPE_OK;
}

auto flush_cpu_writes(cpipe_buffer_t* buffer) -> int {
    if (buffer == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    auto* owner = buffer_from_handle(buffer);
    if (owner == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    owner->flush_cpu_writes();
    return CPIPE_OK;
}

auto submit_halide(cpipe_compute_t* context, const char* aot_id,
                   const cpipe_buffer_t* const* inputs, std::size_t n_in,
                   cpipe_buffer_t* const* outputs, std::size_t n_out) -> int {
    if (context == nullptr || aot_id == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* compute = reinterpret_cast<ComputeContext*>(context);
    return compute->submit_halide(aot_id, std::span<const cpipe_buffer_t* const>{inputs, n_in},
                                  std::span<cpipe_buffer_t* const>{outputs, n_out});
}

auto unsupported_submit_slang(cpipe_compute_t*, const char*, const char*,
                              const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                              std::size_t, const void*, std::size_t) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**) -> int {
    return CPIPE_UNSUPPORTED;
}

auto ignore_marker(cpipe_compute_t*, const char*) -> void {}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
// NOLINTEND(readability-named-parameter,bugprone-easily-swappable-parameters)

const cpipe_buffer_suite_v1 kBufferSuite{&get_dims,   &get_format,      &get_kind,
                                         &get_stride, &get_color_role,  &lock_cpu,
                                         &unlock_cpu, &flush_cpu_writes};

const cpipe_compute_suite_v1 kComputeSuite{&submit_halide, &unsupported_submit_slang,
                                           &unsupported_request_scratch, &ignore_marker};

}  // namespace

class ComputeContext::Impl {
public:
    explicit Impl(std::size_t workers) : executor_(workers), worker_count_(workers) {}

    [[nodiscard]] auto executor() noexcept -> tf::Executor& {
        return executor_;
    }

    [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
        return worker_count_;
    }

    [[nodiscard]] auto halide_entries() noexcept
        -> std::unordered_map<std::string, HalideFilterEntry>& {
        return halide_entries_;
    }

private:
    tf::Executor executor_;
    std::size_t worker_count_ = 0;
    std::unordered_map<std::string, HalideFilterEntry> halide_entries_;
};

ComputeContext::ComputeContext() : impl_(std::make_unique<Impl>(default_worker_count())) {
    static_cast<void>(halide_set_custom_do_par_for(&taskflow_do_par_for));
    g_halide_executor.store(&impl_->executor(), std::memory_order_release);
}

ComputeContext::~ComputeContext() {
    auto* expected = &impl_->executor();
    static_cast<void>(
        g_halide_executor.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
}

auto ComputeContext::handle() noexcept -> cpipe_compute_t* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<cpipe_compute_t*>(this);
}

auto ComputeContext::executor_worker_count() const noexcept -> std::size_t {
    return impl_->worker_count();
}

auto ComputeContext::register_halide(std::string_view aot_id, HalideFilterEntry entry) -> void {
    if (aot_id.empty() || entry == nullptr) {
        return;
    }
    impl_->halide_entries().insert_or_assign(std::string{aot_id}, entry);
}

auto ComputeContext::submit_halide(std::string_view aot_id,
                                   std::span<const cpipe_buffer_t* const> inputs,
                                   std::span<cpipe_buffer_t* const> outputs) -> int {
    auto& halide_entries = impl_->halide_entries();
    const auto found = halide_entries.find(std::string{aot_id});
    if (found == halide_entries.end()) {
        return CPIPE_UNSUPPORTED;
    }

    auto input_adapters = std::vector<std::unique_ptr<HalideBufferAdapter>>{};
    input_adapters.reserve(inputs.size());
    auto input_buffers = std::vector<halide_buffer_t*>{};
    input_buffers.reserve(inputs.size());
    for (const auto* input : inputs) {
        auto adapter = std::make_unique<HalideBufferAdapter>(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            const_cast<cpipe_buffer_t*>(input), compute::IBuffer::CpuAccess::Read);
        if (adapter->status() != CPIPE_OK) {
            return adapter->status();
        }
        input_buffers.push_back(adapter->get());
        input_adapters.push_back(std::move(adapter));
    }

    auto output_adapters = std::vector<std::unique_ptr<HalideBufferAdapter>>{};
    output_adapters.reserve(outputs.size());
    auto output_buffers = std::vector<halide_buffer_t*>{};
    output_buffers.reserve(outputs.size());
    for (auto* output : outputs) {
        auto adapter =
            std::make_unique<HalideBufferAdapter>(output, compute::IBuffer::CpuAccess::ReadWrite);
        if (adapter->status() != CPIPE_OK) {
            return adapter->status();
        }
        output_buffers.push_back(adapter->get());
        output_adapters.push_back(std::move(adapter));
    }

    return found->second(std::span<halide_buffer_t* const>{input_buffers},
                         std::span<halide_buffer_t* const>{output_buffers});
}

auto as_cpipe_buffer(compute::IBuffer& buffer) noexcept -> cpipe_buffer_t* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<cpipe_buffer_t*>(&buffer);
}

auto as_cpipe_buffer(const compute::IBuffer& buffer) noexcept -> const cpipe_buffer_t* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const cpipe_buffer_t*>(&buffer);
}

auto buffer_suite_v1() noexcept -> const cpipe_buffer_suite_v1& {
    return kBufferSuite;
}

auto compute_suite_v1() noexcept -> const cpipe_compute_suite_v1& {
    return kComputeSuite;
}

auto halide_custom_parallel_for_calls() noexcept -> std::uint64_t {
    return g_halide_parallel_for_calls.load(std::memory_order_relaxed);
}

}  // namespace cpipe::runtime
