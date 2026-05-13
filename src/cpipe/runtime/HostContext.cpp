// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

namespace cpipe::runtime {

HostContext::HostContext() {
    buffer_suite_.get_dims = &HostContext::get_dims;
    buffer_suite_.get_format = &HostContext::get_format;
    buffer_suite_.get_kind = &HostContext::get_kind;
    buffer_suite_.get_stride = &HostContext::get_stride;
    buffer_suite_.get_color_role = &HostContext::get_color_role;
    buffer_suite_.lock_cpu = &HostContext::lock_cpu;
    buffer_suite_.unlock_cpu = &HostContext::unlock_cpu;
    buffer_suite_.flush_cpu_writes = &HostContext::flush_cpu_writes;

    compute_suite_.submit_halide = &HostContext::submit_halide;
    compute_suite_.submit_slang = &HostContext::submit_slang;
    compute_suite_.request_scratch = &HostContext::request_scratch;
    compute_suite_.record_marker = &HostContext::record_marker;

    inference_suite_.submit_inference = &HostContext::submit_inference;

    host_.abi_major = CPIPE_ABI_MAJOR;
    host_.abi_minor = CPIPE_ABI_MINOR;
    host_.get_suite = &HostContext::get_suite;
    host_.log = &HostContext::log;
    host_.alloc = &HostContext::alloc;
    host_.free = &HostContext::free;
}

cpipe_host_t* HostContext::host() noexcept {
    return &host_;
}

const void* HostContext::get_suite(cpipe_host_t* self, const char* suite_name, int version) {
    if (self == nullptr || version != 1) {
        return nullptr;
    }

    auto* context = reinterpret_cast<HostContext*>(reinterpret_cast<char*>(self) -
                                                   offsetof(HostContext, host_));
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
    std::clog << "cpipe plugin log[" << level << "]: " << msg << '\n';
}

void* HostContext::alloc(cpipe_host_t*, std::size_t bytes) {
    return std::malloc(bytes);
}

void HostContext::free(cpipe_host_t*, void* ptr) {
    std::free(ptr);
}

int HostContext::get_dims(const cpipe_buffer_t* buffer, std::uint8_t* ndim,
                          std::uint32_t out_dims[8]) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = impl->layout();
    *ndim = layout.ndim;
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        out_dims[i] = layout.dims[i];
    }
    return CPIPE_OK;
}

int HostContext::get_format(const cpipe_buffer_t* buffer, int* out_format) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_format == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_format = static_cast<int>(impl->layout().format);
    return CPIPE_OK;
}

int HostContext::get_kind(const cpipe_buffer_t* buffer, int* out_kind) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_kind == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_kind = static_cast<int>(impl->layout().kind);
    return CPIPE_OK;
}

int HostContext::get_stride(const cpipe_buffer_t* buffer, std::uint64_t out_stride[8]) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_stride == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = impl->layout();
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        out_stride[i] = layout.stride[i];
    }
    return CPIPE_OK;
}

int HostContext::get_color_role(const cpipe_buffer_t* buffer, const char** out_role) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_role == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_role = impl->color_role().data();
    return CPIPE_OK;
}

int HostContext::lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || ptr == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *ptr = impl->lock_cpu(static_cast<compute::IBuffer::CpuAccess>(access));
    return CPIPE_OK;
}

int HostContext::unlock_cpu(cpipe_buffer_t* buffer) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->unlock_cpu();
    return CPIPE_OK;
}

int HostContext::flush_cpu_writes(cpipe_buffer_t* buffer) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->flush_cpu_writes();
    return CPIPE_OK;
}

int HostContext::submit_halide(cpipe_compute_t* compute_handle, const char* aot_id,
                               const cpipe_buffer_t* const* inputs, std::size_t n_in,
                               cpipe_buffer_t* const* outputs, std::size_t n_out) {
    if (compute_handle == nullptr || aot_id == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    std::vector<std::shared_ptr<compute::IBuffer>> input_buffers;
    input_buffers.reserve(n_in);
    for (std::size_t i = 0; i < n_in; ++i) {
        input_buffers.push_back(buffer_from_handle(inputs[i]));
    }

    std::vector<std::shared_ptr<compute::IBuffer>> output_buffers;
    output_buffers.reserve(n_out);
    for (std::size_t i = 0; i < n_out; ++i) {
        output_buffers.push_back(buffer_from_handle(outputs[i]));
    }

    auto* context = reinterpret_cast<ComputeContext*>(compute_handle);
    return context->submit_halide(std::string_view{aot_id}, input_buffers, output_buffers);
}

int HostContext::submit_slang(cpipe_compute_t*, const char*, const char*,
                              const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                              std::size_t, const void*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

int HostContext::request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**) {
    return CPIPE_UNSUPPORTED;
}

void HostContext::record_marker(cpipe_compute_t*, const char*) {}

int HostContext::submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                  std::size_t, cpipe_buffer_t* const*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::runtime
