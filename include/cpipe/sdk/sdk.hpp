// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace cpipe::sdk {

struct Error {
    cpipe_status_t code = CPIPE_FAILED;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

class Buffer {
public:
    Buffer(cpipe_buffer_t* impl, const cpipe_buffer_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] std::uint32_t width() const noexcept {
        return dim(0);
    }

    [[nodiscard]] std::uint32_t height() const noexcept {
        return dim(1);
    }

    [[nodiscard]] std::uint32_t depth() const noexcept {
        return dim(2);
    }

    [[nodiscard]] int format() const noexcept {
        int value = 0;
        if (suite_ == nullptr || suite_->get_format == nullptr || impl_ == nullptr) {
            return value;
        }
        static_cast<void>(suite_->get_format(impl_, &value));
        return value;
    }

    [[nodiscard]] int kind() const noexcept {
        int value = 0;
        if (suite_ == nullptr || suite_->get_kind == nullptr || impl_ == nullptr) {
            return value;
        }
        static_cast<void>(suite_->get_kind(impl_, &value));
        return value;
    }

    [[nodiscard]] std::string_view color_role() const noexcept {
        const char* role = "";
        if (suite_ == nullptr || suite_->get_color_role == nullptr || impl_ == nullptr) {
            return role;
        }
        static_cast<void>(suite_->get_color_role(impl_, &role));
        return role == nullptr ? std::string_view{} : std::string_view{role};
    }

    enum class Access { Read, Write, ReadWrite };

    [[nodiscard]] Result<std::span<std::byte>> lock_cpu(Access access) {
        if (suite_ == nullptr || suite_->lock_cpu == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "buffer CPU locking unavailable"});
        }
        void* ptr = nullptr;
        const auto status =
            static_cast<cpipe_status_t>(suite_->lock_cpu(impl_, static_cast<int>(access), &ptr));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "buffer CPU locking failed"});
        }
        return std::span<std::byte>{static_cast<std::byte*>(ptr), 0};
    }

    void unlock_cpu() {
        if (suite_ != nullptr && suite_->unlock_cpu != nullptr && impl_ != nullptr) {
            static_cast<void>(suite_->unlock_cpu(impl_));
        }
    }

    void flush_cpu_writes() {
        if (suite_ != nullptr && suite_->flush_cpu_writes != nullptr && impl_ != nullptr) {
            static_cast<void>(suite_->flush_cpu_writes(impl_));
        }
    }

    [[nodiscard]] cpipe_buffer_t* impl() const noexcept {
        return impl_;
    }

private:
    [[nodiscard]] std::uint32_t dim(std::uint8_t index) const noexcept {
        if (suite_ == nullptr || suite_->get_dims == nullptr || impl_ == nullptr) {
            return 0;
        }
        std::uint8_t ndim = 0;
        std::uint32_t dims[8] = {};
        if (suite_->get_dims(impl_, &ndim, dims) != CPIPE_OK || index >= ndim) {
            return 0;
        }
        return dims[index];
    }

    cpipe_buffer_t* impl_ = nullptr;
    const cpipe_buffer_suite_v1* suite_ = nullptr;
};

class ComputeContext {
public:
    ComputeContext(cpipe_compute_t* impl, const cpipe_compute_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] Result<void> submit_halide(std::string_view aot_id,
                                             std::span<const Buffer*> inputs,
                                             std::span<Buffer*> outputs) {
        if (suite_ == nullptr || suite_->submit_halide == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "Halide submission unavailable"});
        }
        const auto id = std::string(aot_id);
        const auto raw_inputs = collect_inputs(inputs);
        const auto raw_outputs = collect_outputs(outputs);
        const auto status = static_cast<cpipe_status_t>(
            suite_->submit_halide(impl_, id.c_str(), raw_inputs.data(), raw_inputs.size(),
                                  raw_outputs.data(), raw_outputs.size()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "Halide submission failed"});
        }
        return {};
    }

    [[nodiscard]] Result<void> submit_slang(std::string_view module_id, std::string_view entry,
                                            std::span<const Buffer*> inputs,
                                            std::span<Buffer*> outputs,
                                            std::span<const std::byte> push_constants = {}) {
        if (suite_ == nullptr || suite_->submit_slang == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "Slang submission unavailable"});
        }
        const auto module = std::string(module_id);
        const auto entry_name = std::string(entry);
        const auto raw_inputs = collect_inputs(inputs);
        const auto raw_outputs = collect_outputs(outputs);
        const auto status = static_cast<cpipe_status_t>(suite_->submit_slang(
            impl_, module.c_str(), entry_name.c_str(), raw_inputs.data(), raw_inputs.size(),
            raw_outputs.data(), raw_outputs.size(), push_constants.data(), push_constants.size()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "Slang submission failed"});
        }
        return {};
    }

    [[nodiscard]] Result<Buffer*> request_scratch(std::uint64_t bytes, int kind) {
        if (suite_ == nullptr || suite_->request_scratch == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "scratch allocation unavailable"});
        }
        cpipe_buffer_t* out = nullptr;
        const auto status =
            static_cast<cpipe_status_t>(suite_->request_scratch(impl_, bytes, kind, &out));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "scratch allocation failed"});
        }
        scratch_.emplace_back(out, nullptr);
        return &scratch_.back();
    }

    void mark(std::string_view label) noexcept {
        if (suite_ == nullptr || suite_->record_marker == nullptr) {
            return;
        }
        const auto text = std::string(label);
        suite_->record_marker(impl_, text.c_str());
    }

    [[nodiscard]] static std::vector<const cpipe_buffer_t*> collect_inputs(
        std::span<const Buffer*> buffers) {
        std::vector<const cpipe_buffer_t*> raw;
        raw.reserve(buffers.size());
        for (const auto* buffer : buffers) {
            raw.push_back(buffer == nullptr ? nullptr : buffer->impl());
        }
        return raw;
    }

    [[nodiscard]] static std::vector<cpipe_buffer_t*> collect_outputs(std::span<Buffer*> buffers) {
        std::vector<cpipe_buffer_t*> raw;
        raw.reserve(buffers.size());
        for (const auto* buffer : buffers) {
            raw.push_back(buffer == nullptr ? nullptr : buffer->impl());
        }
        return raw;
    }

private:
    cpipe_compute_t* impl_ = nullptr;
    const cpipe_compute_suite_v1* suite_ = nullptr;
    std::vector<Buffer> scratch_;
};

class InferenceContext {
public:
    InferenceContext(cpipe_inference_t* impl, const cpipe_inference_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] Result<void> submit(std::string_view model_id, std::span<const Buffer*> inputs,
                                      std::span<Buffer*> outputs) {
        if (suite_ == nullptr || suite_->submit_inference == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "inference unavailable"});
        }
        const auto model = std::string(model_id);
        const auto raw_inputs = ComputeContext::collect_inputs(inputs);
        const auto raw_outputs = ComputeContext::collect_outputs(outputs);
        const auto status = static_cast<cpipe_status_t>(
            suite_->submit_inference(impl_, model.c_str(), raw_inputs.data(), raw_inputs.size(),
                                     raw_outputs.data(), raw_outputs.size()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "inference submission failed"});
        }
        return {};
    }

private:
    cpipe_inference_t* impl_ = nullptr;
    const cpipe_inference_suite_v1* suite_ = nullptr;
};

class ParamView {
public:
    ParamView(const cpipe_props_t* impl, const cpipe_param_suite_v1* suite) noexcept
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] double d(std::string_view key) const {
        double out = 0.0;
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_double != nullptr) {
            static_cast<void>(suite_->get_double(impl_, text.c_str(), &out));
        }
        return out;
    }

    [[nodiscard]] std::int64_t i(std::string_view key) const {
        std::int64_t out = 0;
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_int != nullptr) {
            static_cast<void>(suite_->get_int(impl_, text.c_str(), &out));
        }
        return out;
    }

    [[nodiscard]] bool b(std::string_view key) const {
        int out = 0;
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_bool != nullptr) {
            static_cast<void>(suite_->get_bool(impl_, text.c_str(), &out));
        }
        return out != 0;
    }

    [[nodiscard]] std::string_view e(std::string_view key) const {
        const char* out = "";
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_enum != nullptr) {
            static_cast<void>(suite_->get_enum(impl_, text.c_str(), &out));
        }
        return out == nullptr ? std::string_view{} : std::string_view{out};
    }

    struct Curve {
        std::span<const float> xs;
        std::span<const float> ys;
    };

    [[nodiscard]] Curve curve(std::string_view key) const {
        const float* xs = nullptr;
        const float* ys = nullptr;
        std::size_t n = 0;
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_curve != nullptr) {
            static_cast<void>(suite_->get_curve(impl_, text.c_str(), &xs, &ys, &n));
        }
        return {std::span<const float>{xs, n}, std::span<const float>{ys, n}};
    }

    [[nodiscard]] std::array<float, 4> color(std::string_view key) const {
        std::array<float, 4> out{};
        const auto text = std::string(key);
        if (suite_ != nullptr && suite_->get_color != nullptr) {
            static_cast<void>(suite_->get_color(impl_, text.c_str(), out.data()));
        }
        return out;
    }

private:
    const cpipe_props_t* impl_ = nullptr;
    const cpipe_param_suite_v1* suite_ = nullptr;
};

class Node {
public:
    virtual ~Node() = default;

    virtual Result<void> create(const ParamView&) {
        return {};
    }

    virtual Result<void> prepare(ComputeContext&, InferenceContext*, const ParamView&) {
        return {};
    }

    virtual Result<void> process(ComputeContext&, InferenceContext*, const ParamView&,
                                 std::span<const Buffer*> inputs, std::span<Buffer*> outputs) = 0;
};

namespace detail {

[[nodiscard]] inline const void* suite(cpipe_host_t* host, const char* name) {
    if (host == nullptr || host->get_suite == nullptr) {
        return nullptr;
    }
    return host->get_suite(host, name, 1);
}

[[nodiscard]] inline int status_code(const Result<void>& result) {
    if (result) {
        return CPIPE_OK;
    }
    return result.error().code;
}

inline void log_exception(cpipe_host_t* host, const char* message) {
    if (host != nullptr && host->log != nullptr) {
        host->log(host, 4, message);
    }
}

template <class T>
int dispatch(const char* action, cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
             void* in_ctx, void* out_ctx) {
    try {
        if (action == nullptr) {
            return CPIPE_INTERNAL_ERROR;
        }

        auto* param_suite = static_cast<const cpipe_param_suite_v1*>(suite(host, "param"));
        ParamView param_view(params, param_suite);

        if (std::strcmp(action, CPIPE_ACTION_DESCRIBE) == 0) {
            return CPIPE_OK;
        }

        if (std::strcmp(action, CPIPE_ACTION_CREATE) == 0) {
            auto* instance = new T();
            const auto result = instance->create(param_view);
            if (!result) {
                delete instance;
                return result.error().code;
            }
            if (out_ctx != nullptr) {
                *static_cast<void**>(out_ctx) = instance;
            }
            return CPIPE_OK;
        }

        if (std::strcmp(action, CPIPE_ACTION_DESTROY) == 0) {
            delete static_cast<T*>(in_ctx);
            return CPIPE_OK;
        }

        auto* instance = reinterpret_cast<T*>(node);
        if (instance == nullptr) {
            return CPIPE_INTERNAL_ERROR;
        }

        auto* compute_suite = static_cast<const cpipe_compute_suite_v1*>(suite(host, "compute"));
        auto* inference_suite =
            static_cast<const cpipe_inference_suite_v1*>(suite(host, "inference"));

        if (std::strcmp(action, CPIPE_ACTION_PREPARE) == 0) {
            ComputeContext compute(static_cast<cpipe_compute_t*>(in_ctx), compute_suite);
            InferenceContext inference(nullptr, inference_suite);
            return status_code(instance->prepare(compute, &inference, param_view));
        }

        if (std::strcmp(action, CPIPE_ACTION_PROCESS) == 0) {
            auto* process_ctx = static_cast<cpipe_process_ctx*>(in_ctx);
            if (process_ctx == nullptr) {
                return CPIPE_INTERNAL_ERROR;
            }
            ComputeContext compute(process_ctx->compute, compute_suite);
            InferenceContext inference(process_ctx->inference, inference_suite);

            std::vector<Buffer> inputs;
            std::vector<const Buffer*> input_ptrs;
            inputs.reserve(process_ctx->n_in);
            input_ptrs.reserve(process_ctx->n_in);
            for (std::size_t i = 0; i < process_ctx->n_in; ++i) {
                inputs.emplace_back(const_cast<cpipe_buffer_t*>(process_ctx->inputs[i]), nullptr);
                input_ptrs.push_back(&inputs.back());
            }

            std::vector<Buffer> outputs;
            std::vector<Buffer*> output_ptrs;
            outputs.reserve(process_ctx->n_out);
            output_ptrs.reserve(process_ctx->n_out);
            for (std::size_t i = 0; i < process_ctx->n_out; ++i) {
                outputs.emplace_back(process_ctx->outputs[i], nullptr);
                output_ptrs.push_back(&outputs.back());
            }

            return status_code(
                instance->process(compute, &inference, param_view, input_ptrs, output_ptrs));
        }

        return CPIPE_REPLY_DEFAULT;
    } catch (const std::exception& ex) {
        log_exception(host, ex.what());
        return CPIPE_INTERNAL_ERROR;
    } catch (...) {
        log_exception(host, "unknown plugin exception");
        return CPIPE_INTERNAL_ERROR;
    }
}

}  // namespace detail

}  // namespace cpipe::sdk
