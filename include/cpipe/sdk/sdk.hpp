// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

namespace cpipe::sdk {

enum class CpuAccess : int {
    Read = CPIPE_CPU_ACCESS_READ,
    Write = CPIPE_CPU_ACCESS_WRITE,
    ReadWrite = CPIPE_CPU_ACCESS_READ_WRITE,
};

struct Error {
    cpipe_status_t code{CPIPE_FAILED};
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

struct Rect2u {
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct CalibrationView {
    bool has_cfa{false};
    std::array<std::uint8_t, 2> cfa_repeat{};
    std::array<std::uint8_t, 16> cfa_pattern{};
    std::array<float, 4> black_level{};
    std::uint32_t white_level{0};
    std::vector<std::uint16_t> linearization_table;
    std::optional<std::array<float, 9>> color_matrix1;
    std::optional<std::array<float, 9>> color_matrix2;
    std::optional<std::array<float, 9>> forward_matrix1;
    std::optional<std::array<float, 9>> forward_matrix2;
};

struct CaptureView {
    std::array<float, 3> as_shot_neutral{1.0F, 1.0F, 1.0F};
};

class BufferMetadata {
public:
    BufferMetadata(const cpipe_metadata_t* impl, const cpipe_metadata_suite_v1* suite)
        : impl_(impl), suite_(suite) {}

    [[nodiscard]] Result<CalibrationView> calibration() const {
        if (suite_ == nullptr || suite_->get_calibration == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_NEED_METADATA, "metadata suite unavailable"});
        }

        cpipe_calibration_view raw{};
        const auto status = static_cast<cpipe_status_t>(suite_->get_calibration(impl_, &raw));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "get_calibration failed"});
        }

        CalibrationView out{};
        out.has_cfa = raw.has_cfa != 0;
        std::copy(std::begin(raw.cfa_repeat), std::end(raw.cfa_repeat), out.cfa_repeat.begin());
        std::copy(std::begin(raw.cfa_pattern), std::end(raw.cfa_pattern), out.cfa_pattern.begin());
        std::copy(std::begin(raw.black_level), std::end(raw.black_level), out.black_level.begin());
        out.white_level = raw.white_level;
        if (raw.has_color_matrix1 != 0) {
            out.color_matrix1.emplace();
            std::copy(std::begin(raw.color_matrix1), std::end(raw.color_matrix1),
                      out.color_matrix1->begin());
        }
        if (raw.has_color_matrix2 != 0) {
            out.color_matrix2.emplace();
            std::copy(std::begin(raw.color_matrix2), std::end(raw.color_matrix2),
                      out.color_matrix2->begin());
        }
        if (raw.has_forward_matrix1 != 0) {
            out.forward_matrix1.emplace();
            std::copy(std::begin(raw.forward_matrix1), std::end(raw.forward_matrix1),
                      out.forward_matrix1->begin());
        }
        if (raw.has_forward_matrix2 != 0) {
            out.forward_matrix2.emplace();
            std::copy(std::begin(raw.forward_matrix2), std::end(raw.forward_matrix2),
                      out.forward_matrix2->begin());
        }
        if (raw.has_linearization_table != 0 && raw.get_linearization_table != nullptr) {
            std::size_t total = 0;
            const auto count_status =
                static_cast<cpipe_status_t>(raw.get_linearization_table(impl_, 0, &total, nullptr));
            if (count_status != CPIPE_OK) {
                return tl::unexpected(Error{count_status, "get_linearization_table count failed"});
            }
            out.linearization_table.resize(total);
            if (total > 0) {
                const auto read_status = static_cast<cpipe_status_t>(raw.get_linearization_table(
                    impl_, out.linearization_table.size(), &total, out.linearization_table.data()));
                if (read_status != CPIPE_OK) {
                    return tl::unexpected(
                        Error{read_status, "get_linearization_table read failed"});
                }
                out.linearization_table.resize(total);
            }
        }
        return out;
    }

    [[nodiscard]] Result<CaptureView> capture() const {
        if (suite_ == nullptr || suite_->get_capture == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_NEED_METADATA, "metadata suite unavailable"});
        }

        cpipe_capture_view raw{};
        const auto status = static_cast<cpipe_status_t>(suite_->get_capture(impl_, &raw));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "get_capture failed"});
        }

        CaptureView out{};
        std::copy(std::begin(raw.as_shot_neutral), std::end(raw.as_shot_neutral),
                  out.as_shot_neutral.begin());
        return out;
    }

    [[nodiscard]] std::string_view cs_role() const noexcept {
        if (suite_ == nullptr || suite_->get_cs_role == nullptr || impl_ == nullptr) {
            return {};
        }
        const char* out = nullptr;
        if (suite_->get_cs_role(impl_, &out) != CPIPE_OK || out == nullptr) {
            return {};
        }
        return out;
    }

    [[nodiscard]] std::optional<Rect2u> active_area() const noexcept {
        if (suite_ == nullptr || suite_->get_active_area == nullptr || impl_ == nullptr) {
            return std::nullopt;
        }
        Rect2u rect{};
        if (suite_->get_active_area(impl_, &rect.x, &rect.y, &rect.width, &rect.height) !=
            CPIPE_OK) {
            return std::nullopt;
        }
        if (rect.width == 0 || rect.height == 0) {
            return std::nullopt;
        }
        return rect;
    }

    [[nodiscard]] bool has_step(std::string_view step) const noexcept {
        if (suite_ == nullptr || suite_->has_applied_step == nullptr || impl_ == nullptr) {
            return false;
        }
        const std::string step_string{step};
        int out = 0;
        return suite_->has_applied_step(impl_, step_string.c_str(), &out) == CPIPE_OK && out != 0;
    }

    [[nodiscard]] std::vector<std::string_view> applied_steps() const {
        if (suite_ == nullptr || suite_->list_applied_steps == nullptr || impl_ == nullptr) {
            return {};
        }
        std::size_t total = 0;
        if (suite_->list_applied_steps(impl_, 0, &total, nullptr) != CPIPE_OK || total == 0) {
            return {};
        }
        std::vector<const char*> raw(total);
        if (suite_->list_applied_steps(impl_, raw.size(), &total, raw.data()) != CPIPE_OK) {
            return {};
        }
        std::vector<std::string_view> result;
        result.reserve(total);
        for (const auto* step : raw) {
            if (step != nullptr) {
                result.emplace_back(step);
            }
        }
        return result;
    }

private:
    const cpipe_metadata_t* impl_{nullptr};
    const cpipe_metadata_suite_v1* suite_{nullptr};
};

class MetadataBuilder {
public:
    MetadataBuilder(cpipe_metadata_builder_t* impl, const cpipe_metadata_builder_suite_v1* suite)
        : impl_(impl), suite_(suite) {}

    Result<void> set_cs_role(std::string_view role) {
        if (suite_ == nullptr || suite_->set_cs_role == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "metadata builder suite unavailable"});
        }
        const std::string value{role};
        const auto status = static_cast<cpipe_status_t>(suite_->set_cs_role(impl_, value.c_str()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "set_cs_role failed"});
        }
        return {};
    }

    Result<void> add_applied_step(std::string_view step) {
        if (suite_ == nullptr || suite_->add_applied_step == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "metadata builder suite unavailable"});
        }
        const std::string value{step};
        const auto status =
            static_cast<cpipe_status_t>(suite_->add_applied_step(impl_, value.c_str()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "add_applied_step failed"});
        }
        return {};
    }

    Result<void> clear_cfa() {
        if (suite_ == nullptr || suite_->clear_cfa == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "metadata builder suite unavailable"});
        }
        const auto status = static_cast<cpipe_status_t>(suite_->clear_cfa(impl_));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "clear_cfa failed"});
        }
        return {};
    }

private:
    cpipe_metadata_builder_t* impl_{nullptr};
    const cpipe_metadata_builder_suite_v1* suite_{nullptr};
};

class Buffer {
public:
    Buffer(cpipe_buffer_t* impl, const cpipe_buffer_suite_v1* suite,
           const cpipe_metadata_suite_v1* metadata_suite)
        : impl_(impl), suite_(suite), metadata_suite_(metadata_suite) {}

    [[nodiscard]] cpipe_buffer_t* impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] Result<std::vector<std::uint32_t>> dims() const {
        if (suite_ == nullptr || suite_->get_dims == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_BAD_INDEX, "buffer suite unavailable"});
        }
        std::uint8_t ndim = 0;
        std::uint32_t raw[8]{};
        const auto status = static_cast<cpipe_status_t>(suite_->get_dims(impl_, &ndim, raw));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "get_dims failed"});
        }
        return std::vector<std::uint32_t>{raw, raw + ndim};
    }

    [[nodiscard]] Result<int> format() const {
        if (suite_ == nullptr || suite_->get_format == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_BAD_INDEX, "buffer suite unavailable"});
        }
        int format = 0;
        const auto status = static_cast<cpipe_status_t>(suite_->get_format(impl_, &format));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "get_format failed"});
        }
        return format;
    }

    [[nodiscard]] Result<void*> lock_cpu(CpuAccess access) const {
        if (suite_ == nullptr || suite_->lock_cpu == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_BAD_INDEX, "buffer suite unavailable"});
        }
        void* ptr = nullptr;
        const auto status =
            static_cast<cpipe_status_t>(suite_->lock_cpu(impl_, static_cast<int>(access), &ptr));
        if (status != CPIPE_OK || ptr == nullptr) {
            return tl::unexpected(Error{status, "lock_cpu failed"});
        }
        return ptr;
    }

    Result<void> unlock_cpu() const {
        if (suite_ == nullptr || suite_->unlock_cpu == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_BAD_INDEX, "buffer suite unavailable"});
        }
        const auto status = static_cast<cpipe_status_t>(suite_->unlock_cpu(impl_));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "unlock_cpu failed"});
        }
        return {};
    }

    Result<void> flush_cpu_writes() const {
        if (suite_ == nullptr || suite_->flush_cpu_writes == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_BAD_INDEX, "buffer suite unavailable"});
        }
        const auto status = static_cast<cpipe_status_t>(suite_->flush_cpu_writes(impl_));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "flush_cpu_writes failed"});
        }
        return {};
    }

    [[nodiscard]] const cpipe_buffer_suite_v1* suite() const noexcept {
        return suite_;
    }

    [[nodiscard]] const BufferMetadata* metadata() const noexcept {
        if (suite_ == nullptr || suite_->get_metadata == nullptr || metadata_suite_ == nullptr) {
            return nullptr;
        }
        const cpipe_metadata_t* metadata = nullptr;
        if (suite_->get_metadata(impl_, &metadata) != CPIPE_OK || metadata == nullptr) {
            return nullptr;
        }
        metadata_cache_.emplace(metadata, metadata_suite_);
        return &*metadata_cache_;
    }

    [[nodiscard]] std::string_view cs_role() const noexcept {
        const auto* view = metadata();
        return view == nullptr ? std::string_view{} : view->cs_role();
    }

private:
    cpipe_buffer_t* impl_{nullptr};
    const cpipe_buffer_suite_v1* suite_{nullptr};
    const cpipe_metadata_suite_v1* metadata_suite_{nullptr};
    mutable std::optional<BufferMetadata> metadata_cache_;
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

    Result<void> submit_halide_with_params(std::string_view aot_id, std::span<const Buffer*> inputs,
                                           std::span<Buffer*> outputs,
                                           std::span<const std::byte> param_blob) {
        if (suite_ == nullptr || suite_->submit_halide_with_params == nullptr) {
            return tl::unexpected(Error{CPIPE_UNSUPPORTED, "compute suite params unavailable"});
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
        const auto status = static_cast<cpipe_status_t>(suite_->submit_halide_with_params(
            impl_, id.c_str(), raw_inputs.data(), raw_inputs.size(), raw_outputs.data(),
            raw_outputs.size(), param_blob.data(), param_blob.size()));
        if (status != CPIPE_OK) {
            return tl::unexpected(Error{status, "submit_halide_with_params failed"});
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

    [[nodiscard]] Result<std::string_view> string(std::string_view key) const {
        if (suite_ == nullptr || suite_->get_enum == nullptr || impl_ == nullptr) {
            return tl::unexpected(Error{CPIPE_NEED_PARAM, "param suite unavailable"});
        }
        const std::string key_string{key};
        const char* out = nullptr;
        const auto status =
            static_cast<cpipe_status_t>(suite_->get_enum(impl_, key_string.c_str(), &out));
        if (status != CPIPE_OK || out == nullptr) {
            return tl::unexpected(Error{status, "string param missing"});
        }
        return std::string_view{out};
    }

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
                                 std::span<const Buffer*> inputs, std::span<Buffer*> outputs,
                                 std::span<MetadataBuilder*> out_metadata) = 0;
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
        const auto* metadata_suite =
            static_cast<const cpipe_metadata_suite_v1*>(get_suite(host, "metadata"));
        const auto* metadata_builder_suite = static_cast<const cpipe_metadata_builder_suite_v1*>(
            get_suite(host, "metadata_builder"));
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
                                           buffer_suite, metadata_suite);
                inputs.push_back(&input_buffers.back());
            }

            std::vector<Buffer> output_buffers;
            std::vector<Buffer*> outputs;
            output_buffers.reserve(process->n_out);
            outputs.reserve(process->n_out);
            for (std::size_t i = 0; i < process->n_out; ++i) {
                output_buffers.emplace_back(process->outputs[i], buffer_suite, metadata_suite);
                outputs.push_back(&output_buffers.back());
            }

            std::vector<MetadataBuilder> metadata_builders;
            std::vector<MetadataBuilder*> out_metadata;
            metadata_builders.reserve(process->n_out);
            out_metadata.reserve(process->n_out);
            if (process->out_metadata != nullptr) {
                for (std::size_t i = 0; i < process->n_out; ++i) {
                    metadata_builders.emplace_back(process->out_metadata[i],
                                                   metadata_builder_suite);
                    out_metadata.push_back(&metadata_builders.back());
                }
            }

            return result_to_status(
                instance->process(compute, &inference, param_view, inputs, outputs, out_metadata));
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
