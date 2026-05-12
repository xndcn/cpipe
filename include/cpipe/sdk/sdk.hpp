// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace cpipe::sdk {

struct Error {
    cpipe_status_t code = CPIPE_OK;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

class Buffer {
public:
    Buffer(const cpipe_buffer_t* impl, const cpipe_buffer_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto width() const noexcept -> std::uint32_t;
    [[nodiscard]] auto height() const noexcept -> std::uint32_t;
    [[nodiscard]] auto depth() const noexcept -> std::uint32_t;
    [[nodiscard]] auto format() const noexcept -> int;
    [[nodiscard]] auto kind() const noexcept -> int;
    [[nodiscard]] auto color_role() const noexcept -> std::string_view;

    enum class Access { Read, Write, ReadWrite };

    [[nodiscard]] auto lock_cpu(Access access) -> Result<std::span<std::byte>>;
    auto unlock_cpu() -> void;
    auto flush_cpu_writes() -> void;

    [[nodiscard]] auto handle() const noexcept -> const cpipe_buffer_t* {
        return impl_;
    }
    [[nodiscard]] auto mutable_handle() const noexcept -> cpipe_buffer_t*;

private:
    [[nodiscard]] auto dims() const noexcept
        -> std::pair<std::uint8_t, std::array<std::uint32_t, 8>>;
    [[nodiscard]] auto stride() const noexcept -> std::array<std::uint64_t, 8>;
    [[nodiscard]] auto size_bytes() const noexcept -> std::uint64_t;

    const cpipe_buffer_t* impl_ = nullptr;
    const cpipe_buffer_suite_v1* suite_ = nullptr;
};

class ComputeContext {
public:
    ComputeContext(cpipe_compute_t* impl, const cpipe_compute_suite_v1* suite,
                   const cpipe_buffer_suite_v1* buffer_suite = nullptr) noexcept
        : impl_(impl), suite_(suite), buffer_suite_(buffer_suite) {}

    [[nodiscard]] auto submit_halide(std::string_view aot_id, std::span<const Buffer*> inputs,
                                     std::span<Buffer*> outputs) -> Result<void>;

    [[nodiscard]] auto submit_slang(std::string_view module_id, std::string_view entry,
                                    std::span<const Buffer*> inputs, std::span<Buffer*> outputs,
                                    std::span<const std::byte> push_constants = {}) -> Result<void>;

    [[nodiscard]] auto request_scratch(std::uint64_t bytes, int kind) -> Result<Buffer*>;
    auto mark(std::string_view label) noexcept -> void;

private:
    cpipe_compute_t* impl_ = nullptr;
    const cpipe_compute_suite_v1* suite_ = nullptr;
    const cpipe_buffer_suite_v1* buffer_suite_ = nullptr;
    std::vector<std::unique_ptr<Buffer>> scratch_buffers_;
};

class InferenceContext {
public:
    InferenceContext(cpipe_inference_t* impl, const cpipe_inference_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto submit(std::string_view model_id, std::span<const Buffer*> inputs,
                              std::span<Buffer*> outputs) -> Result<void>;

private:
    cpipe_inference_t* impl_ = nullptr;
    const cpipe_inference_suite_v1* suite_ = nullptr;
};

class ParamView {
public:
    ParamView(const cpipe_props_t* impl, const cpipe_param_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto d(std::string_view key) const -> double;
    [[nodiscard]] auto i(std::string_view key) const -> std::int64_t;
    [[nodiscard]] auto b(std::string_view key) const -> bool;
    [[nodiscard]] auto e(std::string_view key) const -> std::string_view;

    struct Curve {
        std::span<const float> xs;
        std::span<const float> ys;
    };

    [[nodiscard]] auto curve(std::string_view key) const -> Curve;
    [[nodiscard]] auto color(std::string_view key) const -> std::array<float, 4>;

private:
    const cpipe_props_t* impl_ = nullptr;
    const cpipe_param_suite_v1* suite_ = nullptr;
};

class Node {
public:
    virtual ~Node() = default;

    [[nodiscard]] virtual auto create(const ParamView& params) -> Result<void> {
        (void)params;
        return {};
    }
    [[nodiscard]] virtual auto prepare(ComputeContext& compute, InferenceContext* inference,
                                       const ParamView& params) -> Result<void> {
        (void)compute;
        (void)inference;
        (void)params;
        return {};
    }
    [[nodiscard]] virtual auto process(ComputeContext&, InferenceContext*, const ParamView&,
                                       std::span<const Buffer*> inputs, std::span<Buffer*> outputs)
        -> Result<void> = 0;
};

namespace detail {

template <class Suite>
[[nodiscard]] auto get_suite(cpipe_host_t* host, const char* name) noexcept -> const Suite* {
    if (host == nullptr || host->get_suite == nullptr) {
        return nullptr;
    }
    return static_cast<const Suite*>(host->get_suite(host, name, 1));
}

template <class T>
[[nodiscard]] auto instance_from_node(cpipe_node_t* node) noexcept -> T* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<T*>(node);
}

inline auto log_error(cpipe_host_t* host, const char* message) noexcept -> void {
    if (host == nullptr || host->log == nullptr || message == nullptr) {
        return;
    }
    host->log(host, 4, message);
}

inline auto status_to_result(int status, std::string message) -> Result<void> {
    if (status == CPIPE_OK) {
        return {};
    }
    return tl::unexpected(Error{static_cast<cpipe_status_t>(status), std::move(message)});
}

inline auto result_to_status(const Result<void>& result, cpipe_host_t* host) noexcept -> int {
    if (result.has_value()) {
        return CPIPE_OK;
    }
    log_error(host, result.error().message.c_str());
    return result.error().code;
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
template <class T>
auto dispatch(const char* action, cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
              void* in_ctx, void* out_ctx) -> int {
    try {
        if (action == nullptr) {
            return CPIPE_REPLY_DEFAULT;
        }

        const auto* buffer_suite = get_suite<cpipe_buffer_suite_v1>(host, "buffer");
        const auto* compute_suite = get_suite<cpipe_compute_suite_v1>(host, "compute");
        const auto* inference_suite = get_suite<cpipe_inference_suite_v1>(host, "inference");
        const auto* param_suite = get_suite<cpipe_param_suite_v1>(host, "param");
        auto params_view = ParamView{params, param_suite};

        if (std::strcmp(action, CPIPE_ACTION_DESCRIBE) == 0) {
            return CPIPE_REPLY_DEFAULT;
        }

        if (std::strcmp(action, CPIPE_ACTION_CREATE) == 0) {
            if (out_ctx == nullptr) {
                return CPIPE_INTERNAL_ERROR;
            }
            auto instance = std::make_unique<T>();
            const auto result = instance->create(params_view);
            if (!result.has_value()) {
                return result_to_status(result, host);
            }
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            *static_cast<void**>(out_ctx) = instance.release();
            return CPIPE_OK;
        }

        if (std::strcmp(action, CPIPE_ACTION_DESTROY) == 0) {
            auto* instance = static_cast<T*>(in_ctx);
            if (instance == nullptr) {
                instance = instance_from_node<T>(node);
            }
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            delete instance;
            return CPIPE_OK;
        }

        auto* instance = instance_from_node<T>(node);
        if (instance == nullptr) {
            return CPIPE_INTERNAL_ERROR;
        }

        if (std::strcmp(action, CPIPE_ACTION_PREPARE) == 0) {
            auto compute =
                ComputeContext{static_cast<cpipe_compute_t*>(in_ctx), compute_suite, buffer_suite};
            auto result = instance->prepare(compute, nullptr, params_view);
            return result_to_status(result, host);
        }

        if (std::strcmp(action, CPIPE_ACTION_PROCESS) == 0) {
            if (in_ctx == nullptr) {
                return CPIPE_INTERNAL_ERROR;
            }
            const auto* process = static_cast<const cpipe_process_ctx*>(in_ctx);
            auto compute = ComputeContext{process->compute, compute_suite, buffer_suite};
            auto inference = InferenceContext{process->inference, inference_suite};
            auto* inference_ptr = process->inference == nullptr ? nullptr : &inference;

            std::vector<Buffer> input_buffers;
            input_buffers.reserve(process->n_in);
            const auto input_handles =
                std::span<const cpipe_buffer_t* const>{process->inputs, process->n_in};
            for (const auto* input : input_handles) {
                input_buffers.emplace_back(input, buffer_suite);
            }
            std::vector<const Buffer*> input_ptrs;
            input_ptrs.reserve(input_buffers.size());
            for (const auto& input : input_buffers) {
                input_ptrs.push_back(&input);
            }

            std::vector<Buffer> output_buffers;
            output_buffers.reserve(process->n_out);
            const auto output_handles =
                std::span<cpipe_buffer_t* const>{process->outputs, process->n_out};
            for (auto* output : output_handles) {
                output_buffers.emplace_back(output, buffer_suite);
            }
            std::vector<Buffer*> output_ptrs;
            output_ptrs.reserve(output_buffers.size());
            for (auto& output : output_buffers) {
                output_ptrs.push_back(&output);
            }

            auto result =
                instance->process(compute, inference_ptr, params_view, input_ptrs, output_ptrs);
            return result_to_status(result, host);
        }

        return CPIPE_REPLY_DEFAULT;
    } catch (const std::exception& ex) {
        log_error(host, ex.what());
        return CPIPE_INTERNAL_ERROR;
    } catch (...) {
        log_error(host, "unknown plugin exception");
        return CPIPE_INTERNAL_ERROR;
    }
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace detail

inline auto Buffer::dims() const noexcept -> std::pair<std::uint8_t, std::array<std::uint32_t, 8>> {
    auto ndim = std::uint8_t{0};
    auto values = std::array<std::uint32_t, 8>{};
    if (suite_ == nullptr || suite_->get_dims == nullptr || impl_ == nullptr) {
        return {ndim, values};
    }
    if (suite_->get_dims(impl_, &ndim, values.data()) != CPIPE_OK) {
        return {0, {}};
    }
    return {ndim, values};
}

inline auto Buffer::stride() const noexcept -> std::array<std::uint64_t, 8> {
    auto values = std::array<std::uint64_t, 8>{};
    if (suite_ == nullptr || suite_->get_stride == nullptr || impl_ == nullptr) {
        return values;
    }
    if (suite_->get_stride(impl_, values.data()) != CPIPE_OK) {
        return {};
    }
    return values;
}

inline auto Buffer::width() const noexcept -> std::uint32_t {
    const auto [ndim, values] = dims();
    return ndim > 0 ? values[0] : 0U;
}

inline auto Buffer::height() const noexcept -> std::uint32_t {
    const auto [ndim, values] = dims();
    return ndim > 1 ? values[1] : 0U;
}

inline auto Buffer::depth() const noexcept -> std::uint32_t {
    const auto [ndim, values] = dims();
    return ndim > 2 ? values[2] : 0U;
}

inline auto Buffer::format() const noexcept -> int {
    auto out = 0;
    if (suite_ == nullptr || suite_->get_format == nullptr || impl_ == nullptr) {
        return out;
    }
    return suite_->get_format(impl_, &out) == CPIPE_OK ? out : 0;
}

inline auto Buffer::kind() const noexcept -> int {
    auto out = 0;
    if (suite_ == nullptr || suite_->get_kind == nullptr || impl_ == nullptr) {
        return out;
    }
    return suite_->get_kind(impl_, &out) == CPIPE_OK ? out : 0;
}

inline auto Buffer::color_role() const noexcept -> std::string_view {
    const char* out = nullptr;
    if (suite_ == nullptr || suite_->get_color_role == nullptr || impl_ == nullptr) {
        return {};
    }
    return suite_->get_color_role(impl_, &out) == CPIPE_OK && out != nullptr ? out
                                                                             : std::string_view{};
}

inline auto Buffer::size_bytes() const noexcept -> std::uint64_t {
    const auto [ndim, dimensions] = dims();
    if (ndim == 0U) {
        return 0;
    }
    const auto strides = stride();
    const auto outer = static_cast<std::size_t>(ndim - 1U);
    return strides[outer] * dimensions[outer];
}

inline auto Buffer::mutable_handle() const noexcept -> cpipe_buffer_t* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<cpipe_buffer_t*>(impl_);
}

inline auto Buffer::lock_cpu(Access access) -> Result<std::span<std::byte>> {
    if (suite_ == nullptr || suite_->lock_cpu == nullptr || impl_ == nullptr) {
        return tl::unexpected(Error{CPIPE_UNSUPPORTED, "buffer CPU locking is unsupported"});
    }
    void* ptr = nullptr;
    const auto status = suite_->lock_cpu(mutable_handle(), static_cast<int>(access), &ptr);
    if (status != CPIPE_OK) {
        return tl::unexpected(
            Error{static_cast<cpipe_status_t>(status), "buffer CPU locking failed"});
    }
    return std::span<std::byte>{static_cast<std::byte*>(ptr), size_bytes()};
}

inline auto Buffer::unlock_cpu() -> void {
    if (suite_ != nullptr && suite_->unlock_cpu != nullptr && impl_ != nullptr) {
        static_cast<void>(suite_->unlock_cpu(mutable_handle()));
    }
}

inline auto Buffer::flush_cpu_writes() -> void {
    if (suite_ != nullptr && suite_->flush_cpu_writes != nullptr && impl_ != nullptr) {
        static_cast<void>(suite_->flush_cpu_writes(mutable_handle()));
    }
}

inline auto ComputeContext::submit_halide(std::string_view aot_id, std::span<const Buffer*> inputs,
                                          std::span<Buffer*> outputs) -> Result<void> {
    if (suite_ == nullptr || suite_->submit_halide == nullptr) {
        return tl::unexpected(Error{CPIPE_UNSUPPORTED, "Halide submission is unsupported"});
    }
    const auto aot_id_string = std::string{aot_id};
    auto input_handles = std::vector<const cpipe_buffer_t*>{};
    input_handles.reserve(inputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->handle());
    }
    auto output_handles = std::vector<cpipe_buffer_t*>{};
    output_handles.reserve(outputs.size());
    for (const auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->mutable_handle());
    }
    return detail::status_to_result(
        suite_->submit_halide(impl_, aot_id_string.c_str(), input_handles.data(),
                              input_handles.size(), output_handles.data(), output_handles.size()),
        "Halide submission failed");
}

inline auto ComputeContext::submit_slang(std::string_view module_id, std::string_view entry,
                                         std::span<const Buffer*> inputs,
                                         std::span<Buffer*> outputs,
                                         std::span<const std::byte> push_constants)
    -> Result<void> {
    if (suite_ == nullptr || suite_->submit_slang == nullptr) {
        return tl::unexpected(Error{CPIPE_UNSUPPORTED, "Slang submission is unsupported"});
    }
    const auto module_id_string = std::string{module_id};
    const auto entry_string = std::string{entry};
    auto input_handles = std::vector<const cpipe_buffer_t*>{};
    input_handles.reserve(inputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->handle());
    }
    auto output_handles = std::vector<cpipe_buffer_t*>{};
    output_handles.reserve(outputs.size());
    for (const auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->mutable_handle());
    }
    return detail::status_to_result(
        suite_->submit_slang(impl_, module_id_string.c_str(), entry_string.c_str(),
                             input_handles.data(), input_handles.size(), output_handles.data(),
                             output_handles.size(), push_constants.data(), push_constants.size()),
        "Slang submission failed");
}

inline auto ComputeContext::request_scratch(std::uint64_t bytes, int kind) -> Result<Buffer*> {
    if (suite_ == nullptr || suite_->request_scratch == nullptr) {
        return tl::unexpected(Error{CPIPE_UNSUPPORTED, "scratch allocation is unsupported"});
    }
    cpipe_buffer_t* scratch = nullptr;
    const auto status = suite_->request_scratch(impl_, bytes, kind, &scratch);
    if (status != CPIPE_OK) {
        return tl::unexpected(
            Error{static_cast<cpipe_status_t>(status), "scratch allocation failed"});
    }
    scratch_buffers_.push_back(std::make_unique<Buffer>(scratch, buffer_suite_));
    return scratch_buffers_.back().get();
}

inline auto ComputeContext::mark(std::string_view label) noexcept -> void {
    if (suite_ == nullptr || suite_->record_marker == nullptr) {
        return;
    }
    suite_->record_marker(impl_, label.empty() ? "" : label.data());
}

inline auto InferenceContext::submit(std::string_view model_id, std::span<const Buffer*> inputs,
                                     std::span<Buffer*> outputs) -> Result<void> {
    if (suite_ == nullptr || suite_->submit_inference == nullptr) {
        return tl::unexpected(Error{CPIPE_UNSUPPORTED, "inference submission is unsupported"});
    }
    const auto model_id_string = std::string{model_id};
    auto input_handles = std::vector<const cpipe_buffer_t*>{};
    input_handles.reserve(inputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->handle());
    }
    auto output_handles = std::vector<cpipe_buffer_t*>{};
    output_handles.reserve(outputs.size());
    for (const auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->mutable_handle());
    }
    return detail::status_to_result(
        suite_->submit_inference(impl_, model_id_string.c_str(), input_handles.data(),
                                 input_handles.size(), output_handles.data(),
                                 output_handles.size()),
        "inference submission failed");
}

inline auto ParamView::d(std::string_view key) const -> double {
    auto out = 0.0;
    if (suite_ == nullptr || suite_->get_double == nullptr || impl_ == nullptr) {
        return out;
    }
    const auto key_string = std::string{key};
    return suite_->get_double(impl_, key_string.c_str(), &out) == CPIPE_OK ? out : 0.0;
}

inline auto ParamView::i(std::string_view key) const -> std::int64_t {
    auto out = std::int64_t{0};
    if (suite_ == nullptr || suite_->get_int == nullptr || impl_ == nullptr) {
        return out;
    }
    const auto key_string = std::string{key};
    return suite_->get_int(impl_, key_string.c_str(), &out) == CPIPE_OK ? out : 0;
}

inline auto ParamView::b(std::string_view key) const -> bool {
    auto out = 0;
    if (suite_ == nullptr || suite_->get_bool == nullptr || impl_ == nullptr) {
        return false;
    }
    const auto key_string = std::string{key};
    return suite_->get_bool(impl_, key_string.c_str(), &out) == CPIPE_OK && out != 0;
}

inline auto ParamView::e(std::string_view key) const -> std::string_view {
    const char* out = nullptr;
    if (suite_ == nullptr || suite_->get_enum == nullptr || impl_ == nullptr) {
        return {};
    }
    const auto key_string = std::string{key};
    return suite_->get_enum(impl_, key_string.c_str(), &out) == CPIPE_OK && out != nullptr
               ? out
               : std::string_view{};
}

inline auto ParamView::curve(std::string_view key) const -> Curve {
    const float* xs = nullptr;
    const float* ys = nullptr;
    auto count = std::size_t{0};
    if (suite_ == nullptr || suite_->get_curve == nullptr || impl_ == nullptr) {
        return {};
    }
    const auto key_string = std::string{key};
    if (suite_->get_curve(impl_, key_string.c_str(), &xs, &ys, &count) != CPIPE_OK ||
        xs == nullptr || ys == nullptr) {
        return {};
    }
    return Curve{std::span<const float>{xs, count}, std::span<const float>{ys, count}};
}

inline auto ParamView::color(std::string_view key) const -> std::array<float, 4> {
    auto out = std::array<float, 4>{};
    if (suite_ == nullptr || suite_->get_color == nullptr || impl_ == nullptr) {
        return out;
    }
    const auto key_string = std::string{key};
    if (suite_->get_color(impl_, key_string.c_str(), out.data()) != CPIPE_OK) {
        return {};
    }
    return out;
}

}  // namespace cpipe::sdk
