// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <cstdint>

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
    static int get_dims(const cpipe_buffer_t* buffer, std::uint8_t* ndim,
                        std::uint32_t out_dims[8]);
    static int get_format(const cpipe_buffer_t* buffer, int* out_format);
    static int get_kind(const cpipe_buffer_t* buffer, int* out_kind);
    static int get_stride(const cpipe_buffer_t* buffer, std::uint64_t out_stride[8]);
    static int get_color_role(const cpipe_buffer_t* buffer, const char** out_role);
    static int lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr);
    static int unlock_cpu(cpipe_buffer_t* buffer);
    static int flush_cpu_writes(cpipe_buffer_t* buffer);
    static int submit_halide(cpipe_compute_t* compute, const char* aot_id,
                             const cpipe_buffer_t* const* inputs, std::size_t n_in,
                             cpipe_buffer_t* const* outputs, std::size_t n_out);
    static int submit_slang(cpipe_compute_t*, const char*, const char*,
                            const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                            std::size_t, const void*, std::size_t);
    static int request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**);
    static void record_marker(cpipe_compute_t*, const char*);
    static int submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                std::size_t, cpipe_buffer_t* const*, std::size_t);

    cpipe_host_t host_{};
    cpipe_buffer_suite_v1 buffer_suite_{};
    cpipe_compute_suite_v1 compute_suite_{};
    cpipe_param_suite_v1 param_suite_{};
    cpipe_inference_suite_v1 inference_suite_{};
};

}  // namespace cpipe::runtime
