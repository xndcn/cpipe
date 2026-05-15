// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#ifndef CPIPE_SDK_CPIPE_NODE_H
#define CPIPE_SDK_CPIPE_NODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPIPE_ABI_MAJOR 0
#define CPIPE_ABI_MINOR 4

typedef enum {
    CPIPE_OK = 0,
    CPIPE_FAILED = 1,
    CPIPE_REPLY_DEFAULT = 2,
    CPIPE_OOM = 3,
    CPIPE_BAD_PRECISION = 4,
    CPIPE_BAD_INDEX = 5,
    CPIPE_NEED_PARAM = 6,
    CPIPE_INTERNAL_ERROR = 7,
    CPIPE_UNSUPPORTED = 8,
    CPIPE_NEED_METADATA = 9
} cpipe_status_t;

#define CPIPE_ACTION_DESCRIBE "describe"
#define CPIPE_ACTION_CREATE "create"
#define CPIPE_ACTION_DESTROY "destroy"
#define CPIPE_ACTION_PREPARE "prepare"
#define CPIPE_ACTION_PROCESS "process"

typedef struct cpipe_host_s cpipe_host_t;
typedef struct cpipe_node_s cpipe_node_t;
typedef struct cpipe_props_s cpipe_props_t;
typedef struct cpipe_buffer_s cpipe_buffer_t;
typedef struct cpipe_metadata_s cpipe_metadata_t;
typedef struct cpipe_metadata_builder_s cpipe_metadata_builder_t;
typedef struct cpipe_compute_s cpipe_compute_t;
typedef struct cpipe_inference_s cpipe_inference_t;
typedef struct cpipe_ocio_processor_s cpipe_ocio_processor_t;

typedef enum {
    CPIPE_CPU_ACCESS_READ = 0,
    CPIPE_CPU_ACCESS_WRITE = 1,
    CPIPE_CPU_ACCESS_READ_WRITE = 2
} cpipe_cpu_access_t;

typedef struct {
    int (*get_dims)(const cpipe_buffer_t*, uint8_t* ndim, uint32_t out_dims[8]);
    int (*get_format)(const cpipe_buffer_t*, int* out_format);
    int (*get_kind)(const cpipe_buffer_t*, int* out_kind);
    int (*get_stride)(const cpipe_buffer_t*, uint64_t out_stride[8]);
    int (*get_color_role)(const cpipe_buffer_t*, const char** out_role);
    int (*lock_cpu)(cpipe_buffer_t*, int access, void** ptr);
    int (*unlock_cpu)(cpipe_buffer_t*);
    int (*flush_cpu_writes)(cpipe_buffer_t*);
    int (*get_metadata)(const cpipe_buffer_t*, const cpipe_metadata_t** out);
} cpipe_buffer_suite_v1;

typedef struct {
    int has_cfa;
    uint8_t cfa_repeat[2];
    uint8_t cfa_pattern[16];
    float black_level[4];
    uint32_t white_level;
    int has_linearization_table;
    int (*get_linearization_table)(const cpipe_metadata_t*, size_t max_values, size_t* out_n,
                                   uint16_t* out_values);
    int has_color_matrix1;
    float color_matrix1[9];
    int has_color_matrix2;
    float color_matrix2[9];
    int has_forward_matrix1;
    float forward_matrix1[9];
    int has_forward_matrix2;
    float forward_matrix2[9];
    uint16_t calibration_illuminant1;
    uint16_t calibration_illuminant2;
    int (*get_noise_profile)(const cpipe_metadata_t*, size_t max_pairs, size_t* out_n, float* out_a,
                             float* out_b);
} cpipe_calibration_view;

typedef struct {
    int64_t sensor_timestamp_ns;
    int64_t exposure_time_ns;
    int32_t iso;
    float lens_focal_length_mm;
    float lens_aperture;
    float lens_focus_distance_d;
    float as_shot_neutral[3];
    uint8_t orientation;
    uint32_t burst_index;
    uint32_t burst_size;
    int (*get_camera_id)(const cpipe_metadata_t*, char* out, size_t cap);
    int (*get_physical_camera_id)(const cpipe_metadata_t*, char* out, size_t cap);
} cpipe_capture_view;

typedef struct {
    int scheme;
    int has_axis;
    int8_t axis;
    int (*get_scales)(const cpipe_metadata_t*, size_t max, size_t* out_n, float* out);
    int (*get_zero_points)(const cpipe_metadata_t*, size_t max, size_t* out_n, int32_t* out);
} cpipe_tensor_quant_view;

typedef struct {
    int (*get_calibration)(const cpipe_metadata_t*, cpipe_calibration_view* out);
    int (*get_capture)(const cpipe_metadata_t*, cpipe_capture_view* out);
    int (*get_tensor_quant)(const cpipe_metadata_t*, cpipe_tensor_quant_view* out);
    int (*get_cs_role)(const cpipe_metadata_t*, const char** out);
    int (*get_active_area)(const cpipe_metadata_t*, uint32_t* x, uint32_t* y, uint32_t* w,
                           uint32_t* h);
    int (*has_applied_step)(const cpipe_metadata_t*, const char* step, int* out_bool);
    int (*list_applied_steps)(const cpipe_metadata_t*, size_t max, size_t* out_n,
                              const char** out_steps);
    int (*get_blob)(const cpipe_metadata_t*, const char* key, const void** out_ptr,
                    size_t* out_size);
    int (*list_blob_keys)(const cpipe_metadata_t*, size_t max, size_t* out_total,
                          const char** out_keys);
} cpipe_metadata_suite_v1;

typedef struct {
    int (*share_calibration_from)(cpipe_metadata_builder_t*, size_t input_idx);
    int (*clear_calibration)(cpipe_metadata_builder_t*);
    int (*clear_cfa)(cpipe_metadata_builder_t*);
    int (*set_as_shot_neutral)(cpipe_metadata_builder_t*, const float rgb[3]);
    int (*set_orientation)(cpipe_metadata_builder_t*, uint8_t orient);
    int (*set_cs_role)(cpipe_metadata_builder_t*, const char* cs_role);
    int (*add_applied_step)(cpipe_metadata_builder_t*, const char* step);
    int (*remove_applied_step)(cpipe_metadata_builder_t*, const char* step);
    int (*set_active_area)(cpipe_metadata_builder_t*, uint32_t x, uint32_t y, uint32_t w,
                           uint32_t h);
    int (*set_tensor_quant)(cpipe_metadata_builder_t*, int scheme, int has_axis, int8_t axis,
                            const float* scales, size_t n_scales, const int32_t* zero_points,
                            size_t n_zp);
    int (*set_blob)(cpipe_metadata_builder_t*, const char* key, const void* ptr, size_t size);
    int (*merge_from)(cpipe_metadata_builder_t*, size_t input_idx, int policy);
    int (*set_cfa)(cpipe_metadata_builder_t*, const uint8_t repeat[2], const uint8_t pattern[16]);
} cpipe_metadata_builder_suite_v1;

typedef struct {
    int (*submit_halide)(cpipe_compute_t*, const char* aot_id, const cpipe_buffer_t* const* inputs,
                         size_t n_in, cpipe_buffer_t* const* outputs, size_t n_out);
    int (*submit_slang)(cpipe_compute_t*, const char* slang_module_id, const char* entry_point,
                        const cpipe_buffer_t* const* inputs, size_t n_in,
                        cpipe_buffer_t* const* outputs, size_t n_out, const void* push_constants,
                        size_t pc_size);
    int (*request_scratch)(cpipe_compute_t*, uint64_t bytes, int kind, cpipe_buffer_t** out);
    void (*record_marker)(cpipe_compute_t*, const char* label);
    int (*submit_halide_with_params)(cpipe_compute_t*, const char* aot_id,
                                     const cpipe_buffer_t* const* inputs, size_t n_in,
                                     cpipe_buffer_t* const* outputs, size_t n_out,
                                     const void* param_blob, size_t param_blob_size);
    int (*submit_ocio_processor)(cpipe_compute_t*, cpipe_ocio_processor_t* processor,
                                 const cpipe_buffer_t* input, cpipe_buffer_t* output);
} cpipe_compute_suite_v1;

typedef struct {
    int (*submit_inference)(cpipe_inference_t*, const char* model_id,
                            const cpipe_buffer_t* const* inputs, size_t n_in,
                            cpipe_buffer_t* const* outputs, size_t n_out);
} cpipe_inference_suite_v1;

typedef struct {
    int (*get_double)(const cpipe_props_t*, const char* key, double* out);
    int (*get_int)(const cpipe_props_t*, const char* key, int64_t* out);
    int (*get_bool)(const cpipe_props_t*, const char* key, int* out);
    int (*get_enum)(const cpipe_props_t*, const char* key, const char** out);
    int (*get_curve)(const cpipe_props_t*, const char* key, const float** xs, const float** ys,
                     size_t* n);
    int (*get_color)(const cpipe_props_t*, const char* key, float rgba[4]);
} cpipe_param_suite_v1;

struct cpipe_host_s {
    uint32_t abi_major;
    uint32_t abi_minor;
    const void* (*get_suite)(cpipe_host_t* self, const char* suite_name, int version);
    void (*log)(cpipe_host_t* self, int level, const char* msg);
    void* (*alloc)(cpipe_host_t*, size_t);
    void (*free)(cpipe_host_t*, void*);
    cpipe_ocio_processor_t* (*get_ocio_processor)(cpipe_host_t*, const char* config_path,
                                                  const char* src_cs, const char* dst_cs);
};

typedef struct {
    cpipe_compute_t* compute;
    cpipe_inference_t* inference;
    const cpipe_buffer_t** inputs;
    size_t n_in;
    cpipe_buffer_t** outputs;
    size_t n_out;
    cpipe_metadata_builder_t** out_metadata;
} cpipe_process_ctx;

typedef int (*cpipe_main_entry_t)(const char* action, cpipe_host_t* host, cpipe_node_t* node,
                                  cpipe_props_t* params, void* in_ctx, void* out_ctx);

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    const char* node_id;
    const char* node_version;
    const char* manifest_json;
    cpipe_main_entry_t main_entry;
} cpipe_plugin_desc_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPIPE_SDK_CPIPE_NODE_H */
