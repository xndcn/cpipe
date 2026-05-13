// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>

namespace cpipe::runtime {

class HostContext {
public:
    HostContext();

    [[nodiscard]] cpipe_host_t* host() noexcept;

private:
    static const void* get_suite(cpipe_host_t* self, const char* suite_name, int version);
    static void log(cpipe_host_t* self, int level, const char* msg);
    static void* alloc(cpipe_host_t* self, std::size_t bytes);
    static void free(cpipe_host_t* self, void* ptr);
    static int submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                std::size_t, cpipe_buffer_t* const*, std::size_t);

    cpipe_host_t host_{};
    cpipe_buffer_suite_v1 buffer_suite_{};
    cpipe_compute_suite_v1 compute_suite_{};
    cpipe_param_suite_v1 param_suite_{};
    cpipe_inference_suite_v1 inference_suite_{};
};

}  // namespace cpipe::runtime
