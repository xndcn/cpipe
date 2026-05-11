// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <cpipe/runtime/AbiBridge.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cpipe::runtime {
namespace {

HostContext* owner(cpipe_host_t* self) {
    return reinterpret_cast<HostContext*>(self);
}

int buffer_get_dims(const cpipe_buffer_t* handle, std::uint8_t* ndim, std::uint32_t out_dims[8]) {
    if (handle == nullptr || handle->buffer == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = handle->buffer->layout();
    *ndim = layout.ndim;
    for (std::uint8_t i = 0; i < 8; ++i) {
        out_dims[i] = layout.dims[i];
    }
    return CPIPE_OK;
}

int buffer_get_format(const cpipe_buffer_t* handle, int* out) {
    if (handle == nullptr || handle->buffer == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out = static_cast<int>(handle->buffer->layout().format);
    return CPIPE_OK;
}

int buffer_get_kind(const cpipe_buffer_t* handle, int* out) {
    if (handle == nullptr || handle->buffer == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out = static_cast<int>(handle->buffer->layout().kind);
    return CPIPE_OK;
}

int buffer_get_stride(const cpipe_buffer_t* handle, std::uint64_t out_stride[8]) {
    if (handle == nullptr || handle->buffer == nullptr || out_stride == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = handle->buffer->layout();
    for (std::uint8_t i = 0; i < 8; ++i) {
        out_stride[i] = layout.stride[i];
    }
    return CPIPE_OK;
}

int buffer_get_role(const cpipe_buffer_t* handle, const char** out) {
    static constexpr const char kEmpty[] = "";
    if (handle == nullptr || handle->buffer == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto role = handle->buffer->color_role();
    *out = role.empty() ? kEmpty : role.data();
    return CPIPE_OK;
}

int buffer_lock_cpu(cpipe_buffer_t* handle, int access, void** out) {
    if (handle == nullptr || handle->buffer == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    auto mode = compute::IBuffer::CpuAccess::Read;
    if (access == 1) {
        mode = compute::IBuffer::CpuAccess::Write;
    } else if (access == 2) {
        mode = compute::IBuffer::CpuAccess::ReadWrite;
    }
    *out = handle->buffer->lock_cpu(mode);
    return *out == nullptr ? CPIPE_FAILED : CPIPE_OK;
}

int buffer_unlock_cpu(cpipe_buffer_t* handle) {
    if (handle == nullptr || handle->buffer == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    handle->buffer->unlock_cpu();
    return CPIPE_OK;
}

int buffer_flush_cpu_writes(cpipe_buffer_t* handle) {
    if (handle == nullptr || handle->buffer == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    handle->buffer->flush_cpu_writes();
    return CPIPE_OK;
}

int submit_halide(cpipe_compute_t* handle, const char* aot_id, const cpipe_buffer_t* const* inputs,
                  std::size_t n_in, cpipe_buffer_t* const* outputs, std::size_t n_out) {
    if (handle == nullptr || handle->context == nullptr || aot_id == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    std::vector<const compute::IBuffer*> input_buffers;
    std::vector<compute::IBuffer*> output_buffers;
    input_buffers.reserve(n_in);
    output_buffers.reserve(n_out);

    for (std::size_t i = 0; i < n_in; ++i) {
        if (inputs == nullptr || inputs[i] == nullptr || inputs[i]->buffer == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        input_buffers.push_back(inputs[i]->buffer);
    }
    for (std::size_t i = 0; i < n_out; ++i) {
        if (outputs == nullptr || outputs[i] == nullptr || outputs[i]->buffer == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        output_buffers.push_back(outputs[i]->buffer);
    }

    return handle->context->submit_halide(aot_id, input_buffers, output_buffers);
}

int unsupported_submit_slang(cpipe_compute_t*, const char*, const char*,
                             const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                             std::size_t, const void*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**) {
    return CPIPE_UNSUPPORTED;
}

void record_marker(cpipe_compute_t*, const char* label) {
    spdlog::debug("compute marker: {}", label == nullptr ? "" : label);
}

int need_double(const cpipe_props_t*, const char*, double*) {
    return CPIPE_NEED_PARAM;
}

int need_int(const cpipe_props_t*, const char*, std::int64_t*) {
    return CPIPE_NEED_PARAM;
}

int need_bool(const cpipe_props_t*, const char*, int*) {
    return CPIPE_NEED_PARAM;
}

int need_enum(const cpipe_props_t*, const char*, const char**) {
    return CPIPE_NEED_PARAM;
}

int need_curve(const cpipe_props_t*, const char*, const float**, const float**, std::size_t*) {
    return CPIPE_NEED_PARAM;
}

int need_color(const cpipe_props_t*, const char*, float[4]) {
    return CPIPE_NEED_PARAM;
}

int unsupported_submit_inference(cpipe_inference_t* handle, const char* model_id,
                                 const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                                 std::size_t) {
    if (handle != nullptr && handle->context != nullptr && model_id != nullptr) {
        return handle->context->submit(model_id, {}, {});
    }
    return CPIPE_UNSUPPORTED;
}

}  // namespace

HostContext::HostContext() {
    host_.abi_major = CPIPE_ABI_MAJOR;
    host_.abi_minor = CPIPE_ABI_MINOR;
    host_.get_suite = &HostContext::get_suite;
    host_.log = &HostContext::log;
    host_.alloc = &HostContext::alloc;
    host_.free = &HostContext::free;

    buffer_suite_.get_dims = &buffer_get_dims;
    buffer_suite_.get_format = &buffer_get_format;
    buffer_suite_.get_kind = &buffer_get_kind;
    buffer_suite_.get_stride = &buffer_get_stride;
    buffer_suite_.get_color_role = &buffer_get_role;
    buffer_suite_.lock_cpu = &buffer_lock_cpu;
    buffer_suite_.unlock_cpu = &buffer_unlock_cpu;
    buffer_suite_.flush_cpu_writes = &buffer_flush_cpu_writes;

    compute_suite_.submit_halide = &submit_halide;
    compute_suite_.submit_slang = &unsupported_submit_slang;
    compute_suite_.request_scratch = &unsupported_request_scratch;
    compute_suite_.record_marker = &record_marker;

    param_suite_.get_double = &need_double;
    param_suite_.get_int = &need_int;
    param_suite_.get_bool = &need_bool;
    param_suite_.get_enum = &need_enum;
    param_suite_.get_curve = &need_curve;
    param_suite_.get_color = &need_color;

    inference_suite_.submit_inference = &unsupported_submit_inference;
}

cpipe_host_t* HostContext::c_host() noexcept {
    return &host_;
}

const cpipe_host_t* HostContext::c_host() const noexcept {
    return &host_;
}

const void* HostContext::get_suite(cpipe_host_t* self, const char* suite_name, int version) {
    if (self == nullptr || suite_name == nullptr || version != 1) {
        return nullptr;
    }
    auto* context = owner(self);
    if (std::strcmp(suite_name, "buffer") == 0) {
        return &context->buffer_suite_;
    }
    if (std::strcmp(suite_name, "compute") == 0) {
        return &context->compute_suite_;
    }
    if (std::strcmp(suite_name, "param") == 0) {
        return &context->param_suite_;
    }
    if (std::strcmp(suite_name, "inference") == 0) {
        return &context->inference_suite_;
    }
    return nullptr;
}

void HostContext::log(cpipe_host_t*, int level, const char* msg) {
    const auto* text = msg == nullptr ? "" : msg;
    switch (level) {
        case 0:
            spdlog::trace("{}", text);
            break;
        case 1:
            spdlog::debug("{}", text);
            break;
        case 2:
            spdlog::info("{}", text);
            break;
        case 3:
            spdlog::warn("{}", text);
            break;
        default:
            spdlog::error("{}", text);
            break;
    }
}

void* HostContext::alloc(cpipe_host_t*, std::size_t bytes) {
    return std::malloc(bytes);
}

void HostContext::free(cpipe_host_t*, void* ptr) {
    std::free(ptr);
}

}  // namespace cpipe::runtime
