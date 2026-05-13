// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/HostContext.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace cpipe::runtime {

HostContext::HostContext() {
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

int HostContext::submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                  std::size_t, cpipe_buffer_t* const*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::runtime
