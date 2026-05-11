// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HostContext.hpp>

#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

namespace cpipe::runtime {
namespace {

HostContext* owner(cpipe_host_t* self) {
    return reinterpret_cast<HostContext*>(self);
}

int unsupported_buffer_get_dims(const cpipe_buffer_t*, std::uint8_t*, std::uint32_t[8]) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_buffer_get_int(const cpipe_buffer_t*, int*) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_buffer_get_stride(const cpipe_buffer_t*, std::uint64_t[8]) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_buffer_get_role(const cpipe_buffer_t*, const char**) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_lock_cpu(cpipe_buffer_t*, int, void**) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_buffer_mutation(cpipe_buffer_t*) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_submit_halide(cpipe_compute_t*, const char*, const cpipe_buffer_t* const*,
                              std::size_t, cpipe_buffer_t* const*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

int unsupported_submit_slang(cpipe_compute_t*, const char*, const char*,
                             const cpipe_buffer_t* const*, std::size_t,
                             cpipe_buffer_t* const*, std::size_t, const void*, std::size_t) {
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

int unsupported_submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                 std::size_t, cpipe_buffer_t* const*, std::size_t) {
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

    buffer_suite_.get_dims = &unsupported_buffer_get_dims;
    buffer_suite_.get_format = &unsupported_buffer_get_int;
    buffer_suite_.get_kind = &unsupported_buffer_get_int;
    buffer_suite_.get_stride = &unsupported_buffer_get_stride;
    buffer_suite_.get_color_role = &unsupported_buffer_get_role;
    buffer_suite_.lock_cpu = &unsupported_lock_cpu;
    buffer_suite_.unlock_cpu = &unsupported_buffer_mutation;
    buffer_suite_.flush_cpu_writes = &unsupported_buffer_mutation;

    compute_suite_.submit_halide = &unsupported_submit_halide;
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
