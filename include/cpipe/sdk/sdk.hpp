// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

namespace cpipe::sdk {

struct Error {
    cpipe_status_t code{CPIPE_FAILED};
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

class Buffer {
public:
    Buffer(cpipe_buffer_t* impl, const cpipe_buffer_suite_v1* suite) : impl_(impl), suite_(suite) {}

    [[nodiscard]] cpipe_buffer_t* impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] const cpipe_buffer_suite_v1* suite() const noexcept {
        return suite_;
    }

private:
    cpipe_buffer_t* impl_{nullptr};
    const cpipe_buffer_suite_v1* suite_{nullptr};
};

class ComputeContext {
public:
    ComputeContext(cpipe_compute_t* impl, const cpipe_compute_suite_v1* suite)
        : impl_(impl), suite_(suite) {}

    Result<void> submit_halide(std::string_view aot_id, std::span<const Buffer*> inputs,
                               std::span<Buffer*> outputs) {
        if (suite_ == nullptr || suite_->submit_halide == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "compute suite unavailable"});
        }

        std::vector<const cpipe_buffer_t*> raw_inputs;
        raw_inputs.reserve(inputs.size());
        for (const auto* input : inputs) {
            raw_inputs.push_back(input->impl());
        }

        std::vector<cpipe_buffer_t*> raw_outputs;
        raw_outputs.reserve(outputs.size());
        for (const auto* output : outputs) {
            raw_outputs.push_back(output->impl());
        }

        const std::string id{aot_id};
        const auto status = static_cast<cpipe_status_t>(
            suite_->submit_halide(impl_, id.c_str(), raw_inputs.data(), raw_inputs.size(),
                                  raw_outputs.data(), raw_outputs.size()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "submit_halide failed"});
        }
        return {};
    }

private:
    cpipe_compute_t* impl_{nullptr};
    const cpipe_compute_suite_v1* suite_{nullptr};
};

class InferenceContext {
public:
    InferenceContext(cpipe_inference_t* impl, const cpipe_inference_suite_v1* suite)
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] cpipe_inference_t* impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] const cpipe_inference_suite_v1* suite() const noexcept {
        return suite_;
    }

private:
    cpipe_inference_t* impl_{nullptr};
    const cpipe_inference_suite_v1* suite_{nullptr};
};

class ParamView {
public:
    ParamView(cpipe_props_t* impl, const cpipe_param_suite_v1* suite)
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] cpipe_props_t* impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] const cpipe_param_suite_v1* suite() const noexcept {
        return suite_;
    }

private:
    cpipe_props_t* impl_{nullptr};
    const cpipe_param_suite_v1* suite_{nullptr};
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

inline cpipe_status_t result_to_status(const Result<void>& result) noexcept {
    if (result.has_value()) {
        return CPIPE_OK;
    }
    return result.error().code;
}

inline const void* get_suite(cpipe_host_t* host, const char* name) {
    if (host == nullptr || host->get_suite == nullptr) {
        return nullptr;
    }
    return host->get_suite(host, name, 1);
}

template <class T>
int dispatch(const char* action, cpipe_host_t* host, cpipe_node_t* node, cpipe_props_t* params,
             void* in_ctx, void* out_ctx) {
    try {
        const auto* buffer_suite =
            static_cast<const cpipe_buffer_suite_v1*>(get_suite(host, "buffer"));
        const auto* compute_suite =
            static_cast<const cpipe_compute_suite_v1*>(get_suite(host, "compute"));
        const auto* inference_suite =
            static_cast<const cpipe_inference_suite_v1*>(get_suite(host, "inference"));
        const auto* param_suite =
            static_cast<const cpipe_param_suite_v1*>(get_suite(host, "param"));
        ParamView param_view{params, param_suite};

        if (std::strcmp(action, CPIPE_ACTION_DESCRIBE) == 0) {
            return CPIPE_REPLY_DEFAULT;
        }
        if (std::strcmp(action, CPIPE_ACTION_CREATE) == 0) {
            auto* instance = new T();
            const auto status = result_to_status(instance->create(param_view));
            if (status != CPIPE_OK) {
                delete instance;
                return status;
            }
            *static_cast<void**>(out_ctx) = instance;
            return CPIPE_OK;
        }
        if (std::strcmp(action, CPIPE_ACTION_DESTROY) == 0) {
            delete static_cast<T*>(in_ctx);
            return CPIPE_OK;
        }
        if (std::strcmp(action, CPIPE_ACTION_PREPARE) == 0) {
            auto* instance = reinterpret_cast<T*>(node);
            ComputeContext compute{static_cast<cpipe_compute_t*>(in_ctx), compute_suite};
            InferenceContext inference{nullptr, inference_suite};
            return result_to_status(instance->prepare(compute, &inference, param_view));
        }
        if (std::strcmp(action, CPIPE_ACTION_PROCESS) == 0) {
            auto* instance = reinterpret_cast<T*>(node);
            const auto* process = static_cast<const cpipe_process_ctx*>(in_ctx);
            ComputeContext compute{process->compute, compute_suite};
            InferenceContext inference{process->inference, inference_suite};

            std::vector<Buffer> input_buffers;
            std::vector<const Buffer*> inputs;
            input_buffers.reserve(process->n_in);
            inputs.reserve(process->n_in);
            for (std::size_t i = 0; i < process->n_in; ++i) {
                input_buffers.emplace_back(const_cast<cpipe_buffer_t*>(process->inputs[i]),
                                           buffer_suite);
                inputs.push_back(&input_buffers.back());
            }

            std::vector<Buffer> output_buffers;
            std::vector<Buffer*> outputs;
            output_buffers.reserve(process->n_out);
            outputs.reserve(process->n_out);
            for (std::size_t i = 0; i < process->n_out; ++i) {
                output_buffers.emplace_back(process->outputs[i], buffer_suite);
                outputs.push_back(&output_buffers.back());
            }

            return result_to_status(
                instance->process(compute, &inference, param_view, inputs, outputs));
        }
    } catch (...) {
        if (host != nullptr && host->log != nullptr) {
            host->log(host, 4, "plugin dispatch threw an exception");
        }
        return CPIPE_INTERNAL_ERROR;
    }

    return CPIPE_REPLY_DEFAULT;
}

}  // namespace detail

}  // namespace cpipe::sdk
