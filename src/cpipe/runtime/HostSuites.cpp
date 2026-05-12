// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "RuntimeHandles.hpp"

namespace cpipe::runtime {
namespace {

int get_dims(const cpipe_buffer_t* buffer, uint8_t* ndim, uint32_t out_dims[8]) {
    if (buffer == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = buffer->buffer->layout();
    *ndim = layout.ndim;
    for (uint8_t index = 0; index < layout.ndim; ++index) {
        out_dims[index] = layout.dims[index];
    }
    return CPIPE_OK;
}

int get_format(const cpipe_buffer_t* buffer, int* out_format) {
    if (buffer == nullptr || out_format == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_format = static_cast<int>(buffer->buffer->layout().format);
    return CPIPE_OK;
}

int get_kind(const cpipe_buffer_t* buffer, int* out_kind) {
    if (buffer == nullptr || out_kind == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_kind = static_cast<int>(buffer->buffer->layout().kind);
    return CPIPE_OK;
}

int get_stride(const cpipe_buffer_t* buffer, uint64_t out_stride[8]) {
    if (buffer == nullptr || out_stride == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = buffer->buffer->layout();
    for (uint8_t index = 0; index < layout.ndim; ++index) {
        out_stride[index] = layout.stride[index];
    }
    return CPIPE_OK;
}

int get_color_role(const cpipe_buffer_t* buffer, const char** out_role) {
    if (buffer == nullptr || out_role == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_role = buffer->buffer->color_role().data();
    return CPIPE_OK;
}

int lock_cpu(cpipe_buffer_t* buffer, int access, void** out_ptr) {
    if (buffer == nullptr || out_ptr == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    auto* ptr = buffer->buffer->lock_cpu(static_cast<compute::IBuffer::CpuAccess>(access));
    if (ptr == nullptr) {
        return CPIPE_FAILED;
    }
    *out_ptr = ptr;
    return CPIPE_OK;
}

int unlock_cpu(cpipe_buffer_t* buffer) {
    if (buffer == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    buffer->buffer->unlock_cpu();
    return CPIPE_OK;
}

int flush_cpu_writes(cpipe_buffer_t* buffer) {
    if (buffer == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    buffer->buffer->flush_cpu_writes();
    return CPIPE_OK;
}

int submit_halide(cpipe_compute_t* compute, const char* aot_id, const cpipe_buffer_t* const* inputs,
                  std::size_t n_in, cpipe_buffer_t* const* outputs, std::size_t n_out) {
    if (compute == nullptr || compute->context == nullptr || aot_id == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    return compute->context->submit_halide(aot_id, inputs, n_in, outputs, n_out);
}

int submit_slang(cpipe_compute_t*, const char*, const char*, const cpipe_buffer_t* const*,
                 std::size_t, cpipe_buffer_t* const*, std::size_t, const void*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

int request_scratch(cpipe_compute_t*, uint64_t, int, cpipe_buffer_t**) {
    return CPIPE_UNSUPPORTED;
}

void record_marker(cpipe_compute_t*, const char* label) {
    if (label != nullptr) {
        spdlog::debug("compute marker: {}", label);
    }
}

int submit_inference(cpipe_inference_t* inference, const char* model_id,
                     const cpipe_buffer_t* const* inputs, std::size_t n_in,
                     cpipe_buffer_t* const* outputs, std::size_t n_out) {
    if (inference == nullptr || inference->context == nullptr || model_id == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    return inference->context->submit(model_id, inputs, n_in, outputs, n_out);
}

int get_double(const cpipe_props_t* props, const char* key, double* out) {
    if (props == nullptr || key == nullptr || out == nullptr || !props->values.contains(key)) {
        return CPIPE_NEED_PARAM;
    }
    *out = props->values.at(key).get<double>();
    return CPIPE_OK;
}

int get_int(const cpipe_props_t* props, const char* key, int64_t* out) {
    if (props == nullptr || key == nullptr || out == nullptr || !props->values.contains(key)) {
        return CPIPE_NEED_PARAM;
    }
    *out = props->values.at(key).get<int64_t>();
    return CPIPE_OK;
}

int get_bool(const cpipe_props_t* props, const char* key, int* out) {
    if (props == nullptr || key == nullptr || out == nullptr || !props->values.contains(key)) {
        return CPIPE_NEED_PARAM;
    }
    *out = props->values.at(key).get<bool>() ? 1 : 0;
    return CPIPE_OK;
}

int get_enum(const cpipe_props_t* props, const char* key, const char** out) {
    if (props == nullptr || key == nullptr || out == nullptr || !props->values.contains(key)) {
        return CPIPE_NEED_PARAM;
    }
    *out = props->values.at(key).get_ref<const std::string&>().c_str();
    return CPIPE_OK;
}

int get_curve(const cpipe_props_t*, const char*, const float**, const float**, std::size_t*) {
    return CPIPE_UNSUPPORTED;
}

int get_color(const cpipe_props_t*, const char*, float[4]) {
    return CPIPE_UNSUPPORTED;
}

const cpipe_buffer_suite_v1 kBufferSuite{get_dims,       get_format, get_kind,   get_stride,
                                         get_color_role, lock_cpu,   unlock_cpu, flush_cpu_writes};
const cpipe_compute_suite_v1 kComputeSuite{submit_halide, submit_slang, request_scratch,
                                           record_marker};
const cpipe_inference_suite_v1 kInferenceSuite{submit_inference};
const cpipe_param_suite_v1 kParamSuite{get_double, get_int,   get_bool,
                                       get_enum,   get_curve, get_color};

const void* get_suite(cpipe_host_t*, const char* suite_name, int version) {
    if (suite_name == nullptr || version != 1) {
        return nullptr;
    }
    if (std::strcmp(suite_name, "buffer") == 0) {
        return &kBufferSuite;
    }
    if (std::strcmp(suite_name, "compute") == 0) {
        return &kComputeSuite;
    }
    if (std::strcmp(suite_name, "param") == 0) {
        return &kParamSuite;
    }
    if (std::strcmp(suite_name, "inference") == 0) {
        return &kInferenceSuite;
    }
    return nullptr;
}

void log(cpipe_host_t*, int level, const char* msg) {
    if (msg == nullptr) {
        return;
    }
    switch (level) {
        case 0:
        case 1:
            spdlog::debug("plugin: {}", msg);
            break;
        case 2:
            spdlog::info("plugin: {}", msg);
            break;
        case 3:
            spdlog::warn("plugin: {}", msg);
            break;
        default:
            spdlog::error("plugin: {}", msg);
            break;
    }
}

void* alloc(cpipe_host_t*, std::size_t bytes) {
    return std::malloc(bytes);
}

void free_memory(cpipe_host_t*, void* ptr) {
    std::free(ptr);
}

}  // namespace

cpipe_host_t make_host() {
    return cpipe_host_t{CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, get_suite, log, alloc, free_memory};
}

}  // namespace cpipe::runtime
