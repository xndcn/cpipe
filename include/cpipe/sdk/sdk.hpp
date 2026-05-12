// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

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

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::sdk {

struct Error {
    cpipe_status_t code = CPIPE_FAILED;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

namespace detail {

[[nodiscard]] inline auto make_error(cpipe_status_t code,
                                     std::string message) -> tl::unexpected<Error> {
    return tl::unexpected<Error>{Error{code, std::move(message)}};
}

[[nodiscard]] inline auto access_to_abi(int access) noexcept -> int {
    return access;
}

[[nodiscard]] inline auto mutable_buffer(const cpipe_buffer_t* buffer) noexcept -> cpipe_buffer_t* {
    return const_cast<cpipe_buffer_t*>(buffer);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

inline void log_error(cpipe_host_t* host, const char* message) noexcept {
    if (host != nullptr && host->log != nullptr) {
        host->log(host, 4, message);
    }
}

template <class Suite>
[[nodiscard]] auto get_suite(cpipe_host_t* host, const char* name) noexcept -> const Suite* {
    if (host == nullptr || host->get_suite == nullptr) {
        return nullptr;
    }
    return static_cast<const Suite*>(host->get_suite(host, name, 1));
}

}  // namespace detail

class Buffer {
public:
    enum class Access : std::uint8_t { Read = 0, Write = 1, ReadWrite = 2 };

    Buffer(cpipe_buffer_t* impl, const cpipe_buffer_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto native() const noexcept -> cpipe_buffer_t* {
        return impl_;
    }

    [[nodiscard]] auto width() const noexcept -> std::uint32_t;
    [[nodiscard]] auto height() const noexcept -> std::uint32_t;
    [[nodiscard]] auto depth() const noexcept -> std::uint32_t;
    [[nodiscard]] auto format() const noexcept -> int;
    [[nodiscard]] auto kind() const noexcept -> int;
    [[nodiscard]] auto color_role() const noexcept -> std::string_view;

    [[nodiscard]] auto lock_cpu(Access access) -> Result<std::span<std::byte>>;
    void unlock_cpu();
    void flush_cpu_writes();

private:
    cpipe_buffer_t* impl_ = nullptr;
    const cpipe_buffer_suite_v1* suite_ = nullptr;
};

class ComputeContext {
public:
    ComputeContext(cpipe_compute_t* impl, const cpipe_compute_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto submit_halide(std::string_view aot_id, std::span<const Buffer* const> inputs,
                                     std::span<Buffer* const> outputs) -> Result<void>;

    [[nodiscard]] auto submit_slang(std::string_view module_id, std::string_view entry,
                                    std::span<const Buffer* const> inputs,
                                    std::span<Buffer* const> outputs,
                                    std::span<const std::byte> push_constants = {}) -> Result<void>;

    [[nodiscard]] auto request_scratch(std::uint64_t bytes, int kind) -> Result<Buffer*>;
    void mark(std::string_view label) noexcept;

private:
    cpipe_compute_t* impl_ = nullptr;
    const cpipe_compute_suite_v1* suite_ = nullptr;
};

class InferenceContext {
public:
    InferenceContext(cpipe_inference_t* impl, const cpipe_inference_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto submit(std::string_view model_id, std::span<const Buffer* const> inputs,
                              std::span<Buffer* const> outputs) -> Result<void>;

private:
    cpipe_inference_t* impl_ = nullptr;
    const cpipe_inference_suite_v1* suite_ = nullptr;
};

class ParamView {
public:
    struct Curve {
        std::span<const float> xs;
        std::span<const float> ys;
    };

    ParamView(const cpipe_props_t* impl, const cpipe_param_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] auto d(std::string_view key) const -> double;
    [[nodiscard]] auto i(std::string_view key) const -> std::int64_t;
    [[nodiscard]] auto b(std::string_view key) const -> bool;
    [[nodiscard]] auto e(std::string_view key) const -> std::string_view;
    [[nodiscard]] auto curve(std::string_view key) const -> Curve;
    [[nodiscard]] auto color(std::string_view key) const -> std::array<float, 4>;

private:
    const cpipe_props_t* impl_ = nullptr;
    const cpipe_param_suite_v1* suite_ = nullptr;
};

class Node {
public:
    virtual ~Node() = default;

    virtual auto create(const ParamView& params) -> Result<void> {
        (void)params;
        return {};
    }

    virtual auto prepare(ComputeContext& compute, InferenceContext* inference,
                         const ParamView& params) -> Result<void> {
        (void)compute;
        (void)inference;
        (void)params;
        return {};
    }

    virtual auto process(ComputeContext&, InferenceContext*, const ParamView&,
                         std::span<const Buffer*> inputs,
                         std::span<Buffer*> outputs) -> Result<void> = 0;
};

namespace detail {

[[nodiscard]] inline auto status_from_result(const Result<void>& result) noexcept -> int {
    if (!result) {
        return result.error().code;
    }
    return CPIPE_OK;
}

template <class T>
[[nodiscard]] auto state_from_node(cpipe_node_t* node) noexcept -> T* {
    return reinterpret_cast<T*>(node);
}

template <class T>
[[nodiscard]] auto state_from_ctx(void* ctx) noexcept -> T* {
    return static_cast<T*>(ctx);
}

template <class T>
auto dispatch_create(cpipe_host_t* host, cpipe_props_t* params, void* out_ctx) -> int {
    if (out_ctx == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }

    auto instance = std::make_unique<T>();
    auto param_view = ParamView{params, get_suite<cpipe_param_suite_v1>(host, "param")};
    auto created = instance->create(param_view);
    if (!created) {
        return created.error().code;
    }

    *static_cast<void**>(out_ctx) = instance.release();
    return CPIPE_OK;
}

template <class T>
auto dispatch_prepare(cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
                      void* in_ctx) -> int {
    auto* instance = state_from_node<T>(node);
    if (instance == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }

    auto compute = ComputeContext{static_cast<cpipe_compute_t*>(in_ctx),
                                  get_suite<cpipe_compute_suite_v1>(host, "compute")};
    auto inference =
        InferenceContext{nullptr, get_suite<cpipe_inference_suite_v1>(host, "inference")};
    auto param_view = ParamView{params, get_suite<cpipe_param_suite_v1>(host, "param")};
    return status_from_result(instance->prepare(compute, &inference, param_view));
}

template <class T>
auto dispatch_process(cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
                      void* in_ctx) -> int {
    auto* instance = state_from_node<T>(node);
    auto* process = static_cast<cpipe_process_ctx*>(in_ctx);
    if (instance == nullptr || process == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }

    const auto* buffer_suite = get_suite<cpipe_buffer_suite_v1>(host, "buffer");
    std::vector<Buffer> input_buffers;
    std::vector<const Buffer*> input_ptrs;
    input_buffers.reserve(process->n_in);
    input_ptrs.reserve(process->n_in);
    for (std::size_t index = 0; index < process->n_in; ++index) {
        input_buffers.emplace_back(mutable_buffer(process->inputs[index]), buffer_suite);
        input_ptrs.push_back(&input_buffers.back());
    }

    std::vector<Buffer> output_buffers;
    std::vector<Buffer*> output_ptrs;
    output_buffers.reserve(process->n_out);
    output_ptrs.reserve(process->n_out);
    for (std::size_t index = 0; index < process->n_out; ++index) {
        output_buffers.emplace_back(process->outputs[index], buffer_suite);
        output_ptrs.push_back(&output_buffers.back());
    }

    auto compute =
        ComputeContext{process->compute, get_suite<cpipe_compute_suite_v1>(host, "compute")};
    auto inference = InferenceContext{process->inference,
                                      get_suite<cpipe_inference_suite_v1>(host, "inference")};
    auto param_view = ParamView{params, get_suite<cpipe_param_suite_v1>(host, "param")};
    return status_from_result(
        instance->process(compute, &inference, param_view, input_ptrs, output_ptrs));
}

template <class T>
auto dispatch(const char* action, cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
              void* in_ctx, void* out_ctx) -> int {
    try {
        if (action == nullptr) {
            return CPIPE_INTERNAL_ERROR;
        }
        if (std::strcmp(action, CPIPE_ACTION_DESCRIBE) == 0) {
            return CPIPE_REPLY_DEFAULT;
        }
        if (std::strcmp(action, CPIPE_ACTION_CREATE) == 0) {
            return dispatch_create<T>(host, params, out_ctx);
        }
        if (std::strcmp(action, CPIPE_ACTION_DESTROY) == 0) {
            delete state_from_ctx<T>(in_ctx);
            return CPIPE_OK;
        }
        if (std::strcmp(action, CPIPE_ACTION_PREPARE) == 0) {
            return dispatch_prepare<T>(host, node, params, in_ctx);
        }
        if (std::strcmp(action, CPIPE_ACTION_PROCESS) == 0) {
            return dispatch_process<T>(host, node, params, in_ctx);
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

}  // namespace detail

inline auto Buffer::width() const noexcept -> std::uint32_t {
    std::uint8_t ndim = 0;
    std::array<std::uint32_t, 8> dims{};
    if (suite_ == nullptr || suite_->get_dims == nullptr ||
        suite_->get_dims(impl_, &ndim, dims.data()) != CPIPE_OK || ndim < 1U) {
        return 0U;
    }
    return dims[0];
}

inline auto Buffer::height() const noexcept -> std::uint32_t {
    std::uint8_t ndim = 0;
    std::array<std::uint32_t, 8> dims{};
    if (suite_ == nullptr || suite_->get_dims == nullptr ||
        suite_->get_dims(impl_, &ndim, dims.data()) != CPIPE_OK || ndim < 2U) {
        return 0U;
    }
    return dims[1];
}

inline auto Buffer::depth() const noexcept -> std::uint32_t {
    std::uint8_t ndim = 0;
    std::array<std::uint32_t, 8> dims{};
    if (suite_ == nullptr || suite_->get_dims == nullptr ||
        suite_->get_dims(impl_, &ndim, dims.data()) != CPIPE_OK || ndim < 3U) {
        return 1U;
    }
    return dims[2];
}

inline auto Buffer::format() const noexcept -> int {
    int format_value = 0;
    if (suite_ == nullptr || suite_->get_format == nullptr ||
        suite_->get_format(impl_, &format_value) != CPIPE_OK) {
        return 0;
    }
    return format_value;
}

inline auto Buffer::kind() const noexcept -> int {
    int kind_value = 0;
    if (suite_ == nullptr || suite_->get_kind == nullptr ||
        suite_->get_kind(impl_, &kind_value) != CPIPE_OK) {
        return 0;
    }
    return kind_value;
}

inline auto Buffer::color_role() const noexcept -> std::string_view {
    const char* role = nullptr;
    if (suite_ == nullptr || suite_->get_color_role == nullptr ||
        suite_->get_color_role(impl_, &role) != CPIPE_OK || role == nullptr) {
        return {};
    }
    return role;
}

inline auto Buffer::lock_cpu(Access access) -> Result<std::span<std::byte>> {
    if (suite_ == nullptr || suite_->lock_cpu == nullptr) {
        return detail::make_error(CPIPE_UNSUPPORTED, "buffer CPU locking is unavailable");
    }

    void* ptr = nullptr;
    const auto status =
        suite_->lock_cpu(impl_, detail::access_to_abi(static_cast<int>(access)), &ptr);
    if (status != CPIPE_OK) {
        return detail::make_error(static_cast<cpipe_status_t>(status), "buffer CPU lock failed");
    }
    return std::span<std::byte>{static_cast<std::byte*>(ptr), std::size_t{0}};
}

inline void Buffer::unlock_cpu() {
    if (suite_ != nullptr && suite_->unlock_cpu != nullptr) {
        (void)suite_->unlock_cpu(impl_);
    }
}

inline void Buffer::flush_cpu_writes() {
    if (suite_ != nullptr && suite_->flush_cpu_writes != nullptr) {
        (void)suite_->flush_cpu_writes(impl_);
    }
}

inline auto ComputeContext::submit_halide(std::string_view aot_id,
                                          std::span<const Buffer* const> inputs,
                                          std::span<Buffer* const> outputs) -> Result<void> {
    if (suite_ == nullptr || suite_->submit_halide == nullptr) {
        return detail::make_error(CPIPE_UNSUPPORTED, "Halide submission is unavailable");
    }

    std::vector<const cpipe_buffer_t*> input_handles;
    std::vector<cpipe_buffer_t*> output_handles;
    input_handles.reserve(inputs.size());
    output_handles.reserve(outputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->native());
    }
    for (auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->native());
    }

    const std::string id{aot_id};
    const auto status =
        suite_->submit_halide(impl_, id.c_str(), input_handles.data(), input_handles.size(),
                              output_handles.data(), output_handles.size());
    if (status != CPIPE_OK) {
        return detail::make_error(static_cast<cpipe_status_t>(status), "Halide submission failed");
    }
    return {};
}

inline auto ComputeContext::submit_slang(
    std::string_view module_id, std::string_view entry, std::span<const Buffer* const> inputs,
    std::span<Buffer* const> outputs, std::span<const std::byte> push_constants) -> Result<void> {
    if (suite_ == nullptr || suite_->submit_slang == nullptr) {
        return detail::make_error(CPIPE_UNSUPPORTED, "Slang submission is unavailable");
    }

    std::vector<const cpipe_buffer_t*> input_handles;
    std::vector<cpipe_buffer_t*> output_handles;
    input_handles.reserve(inputs.size());
    output_handles.reserve(outputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->native());
    }
    for (auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->native());
    }

    const std::string module{module_id};
    const std::string entry_name{entry};
    const auto status = suite_->submit_slang(
        impl_, module.c_str(), entry_name.c_str(), input_handles.data(), input_handles.size(),
        output_handles.data(), output_handles.size(), push_constants.data(), push_constants.size());
    if (status != CPIPE_OK) {
        return detail::make_error(static_cast<cpipe_status_t>(status), "Slang submission failed");
    }
    return {};
}

inline auto ComputeContext::request_scratch(std::uint64_t bytes, int kind) -> Result<Buffer*> {
    if (suite_ != nullptr && suite_->request_scratch != nullptr) {
        cpipe_buffer_t* scratch = nullptr;
        const auto status = suite_->request_scratch(impl_, bytes, kind, &scratch);
        if (status != CPIPE_OK) {
            return detail::make_error(static_cast<cpipe_status_t>(status),
                                      "scratch buffer request failed");
        }
    }
    return detail::make_error(CPIPE_UNSUPPORTED, "scratch buffers are unavailable in T3");
}

inline void ComputeContext::mark(std::string_view label) noexcept {
    if (suite_ == nullptr || suite_->record_marker == nullptr) {
        return;
    }
    const std::string label_text{label};
    suite_->record_marker(impl_, label_text.c_str());
}

inline auto InferenceContext::submit(std::string_view model_id,
                                     std::span<const Buffer* const> inputs,
                                     std::span<Buffer* const> outputs) -> Result<void> {
    if (suite_ == nullptr || suite_->submit_inference == nullptr) {
        return detail::make_error(CPIPE_UNSUPPORTED, "inference submission is unavailable");
    }

    std::vector<const cpipe_buffer_t*> input_handles;
    std::vector<cpipe_buffer_t*> output_handles;
    input_handles.reserve(inputs.size());
    output_handles.reserve(outputs.size());
    for (const auto* input : inputs) {
        input_handles.push_back(input == nullptr ? nullptr : input->native());
    }
    for (auto* output : outputs) {
        output_handles.push_back(output == nullptr ? nullptr : output->native());
    }

    const std::string model{model_id};
    const auto status =
        suite_->submit_inference(impl_, model.c_str(), input_handles.data(), input_handles.size(),
                                 output_handles.data(), output_handles.size());
    if (status != CPIPE_OK) {
        return detail::make_error(static_cast<cpipe_status_t>(status),
                                  "inference submission failed");
    }
    return {};
}

inline auto ParamView::d(std::string_view key) const -> double {
    double value = 0.0;
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_double != nullptr) {
        (void)suite_->get_double(impl_, key_text.c_str(), &value);
    }
    return value;
}

inline auto ParamView::i(std::string_view key) const -> std::int64_t {
    std::int64_t value = 0;
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_int != nullptr) {
        (void)suite_->get_int(impl_, key_text.c_str(), &value);
    }
    return value;
}

inline auto ParamView::b(std::string_view key) const -> bool {
    int value = 0;
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_bool != nullptr) {
        (void)suite_->get_bool(impl_, key_text.c_str(), &value);
    }
    return value != 0;
}

inline auto ParamView::e(std::string_view key) const -> std::string_view {
    const char* value = nullptr;
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_enum != nullptr) {
        (void)suite_->get_enum(impl_, key_text.c_str(), &value);
    }
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

inline auto ParamView::curve(std::string_view key) const -> Curve {
    const float* xs = nullptr;
    const float* ys = nullptr;
    std::size_t count = 0;
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_curve != nullptr) {
        (void)suite_->get_curve(impl_, key_text.c_str(), &xs, &ys, &count);
    }
    return Curve{std::span<const float>{xs, count}, std::span<const float>{ys, count}};
}

inline auto ParamView::color(std::string_view key) const -> std::array<float, 4> {
    std::array<float, 4> rgba{};
    const std::string key_text{key};
    if (suite_ != nullptr && suite_->get_color != nullptr) {
        (void)suite_->get_color(impl_, key_text.c_str(), rgba.data());
    }
    return rgba;
}

}  // namespace cpipe::sdk
