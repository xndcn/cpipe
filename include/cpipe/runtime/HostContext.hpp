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
    static int get_metadata(const cpipe_buffer_t* buffer, const cpipe_metadata_t** out);
    static int lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr);
    static int unlock_cpu(cpipe_buffer_t* buffer);
    static int flush_cpu_writes(cpipe_buffer_t* buffer);
    static int get_calibration(const cpipe_metadata_t* metadata, cpipe_calibration_view* out);
    static int get_capture(const cpipe_metadata_t* metadata, cpipe_capture_view* out);
    static int get_tensor_quant(const cpipe_metadata_t* metadata, cpipe_tensor_quant_view* out);
    static int get_cs_role(const cpipe_metadata_t* metadata, const char** out);
    static int get_active_area(const cpipe_metadata_t* metadata, std::uint32_t* x, std::uint32_t* y,
                               std::uint32_t* w, std::uint32_t* h);
    static int has_applied_step(const cpipe_metadata_t* metadata, const char* step, int* out_bool);
    static int list_applied_steps(const cpipe_metadata_t* metadata, std::size_t max,
                                  std::size_t* out_n, const char** out_steps);
    static int get_blob(const cpipe_metadata_t* metadata, const char* key, const void** out_ptr,
                        std::size_t* out_size);
    static int list_blob_keys(const cpipe_metadata_t* metadata, std::size_t max,
                              std::size_t* out_total, const char** out_keys);
    static int get_noise_profile(const cpipe_metadata_t* metadata, std::size_t max_pairs,
                                 std::size_t* out_n, float* out_a, float* out_b);
    static int get_camera_id(const cpipe_metadata_t* metadata, char* out, std::size_t cap);
    static int get_physical_camera_id(const cpipe_metadata_t* metadata, char* out, std::size_t cap);
    static int get_scales(const cpipe_metadata_t* metadata, std::size_t max, std::size_t* out_n,
                          float* out);
    static int get_zero_points(const cpipe_metadata_t* metadata, std::size_t max,
                               std::size_t* out_n, std::int32_t* out);
    static int share_calibration_from(cpipe_metadata_builder_t* builder, std::size_t input_idx);
    static int clear_calibration(cpipe_metadata_builder_t* builder);
    static int clear_cfa(cpipe_metadata_builder_t* builder);
    static int set_as_shot_neutral(cpipe_metadata_builder_t* builder, const float rgb[3]);
    static int set_orientation(cpipe_metadata_builder_t* builder, std::uint8_t orient);
    static int set_cs_role(cpipe_metadata_builder_t* builder, const char* cs_role);
    static int add_applied_step(cpipe_metadata_builder_t* builder, const char* step);
    static int remove_applied_step(cpipe_metadata_builder_t* builder, const char* step);
    static int set_active_area(cpipe_metadata_builder_t* builder, std::uint32_t x, std::uint32_t y,
                               std::uint32_t w, std::uint32_t h);
    static int set_tensor_quant(cpipe_metadata_builder_t* builder, int scheme, int has_axis,
                                std::int8_t axis, const float* scales, std::size_t n_scales,
                                const std::int32_t* zero_points, std::size_t n_zp);
    static int set_blob(cpipe_metadata_builder_t* builder, const char* key, const void* ptr,
                        std::size_t size);
    static int merge_from(cpipe_metadata_builder_t* builder, std::size_t input_idx, int policy);
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
    cpipe_metadata_suite_v1 metadata_suite_{};
    cpipe_metadata_builder_suite_v1 metadata_builder_suite_{};
    cpipe_compute_suite_v1 compute_suite_{};
    cpipe_param_suite_v1 param_suite_{};
    cpipe_inference_suite_v1 inference_suite_{};
};

}  // namespace cpipe::runtime
