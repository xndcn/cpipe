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
#define CPIPE_ABI_MINOR 1

typedef enum {
    CPIPE_OK = 0,
    CPIPE_FAILED = 1,
    CPIPE_REPLY_DEFAULT = 2,
    CPIPE_OOM = 3,
    CPIPE_BAD_PRECISION = 4,
    CPIPE_BAD_INDEX = 5,
    CPIPE_NEED_PARAM = 6,
    CPIPE_INTERNAL_ERROR = 7,
    CPIPE_UNSUPPORTED = 8
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
typedef struct cpipe_compute_s cpipe_compute_t;
typedef struct cpipe_inference_s cpipe_inference_t;

typedef struct {
    int (*get_dims)(const cpipe_buffer_t*, uint8_t* ndim, uint32_t out_dims[8]);
    int (*get_format)(const cpipe_buffer_t*, int* out_format);
    int (*get_kind)(const cpipe_buffer_t*, int* out_kind);
    int (*get_stride)(const cpipe_buffer_t*, uint64_t out_stride[8]);
    int (*get_color_role)(const cpipe_buffer_t*, const char** out_role);
    int (*lock_cpu)(cpipe_buffer_t*, int access, void** ptr);
    int (*unlock_cpu)(cpipe_buffer_t*);
    int (*flush_cpu_writes)(cpipe_buffer_t*);
} cpipe_buffer_suite_v1;

typedef struct {
    int (*submit_halide)(cpipe_compute_t*, const char* aot_id, const cpipe_buffer_t* const* inputs,
                         size_t n_in, cpipe_buffer_t* const* outputs, size_t n_out);
    int (*submit_slang)(cpipe_compute_t*, const char* slang_module_id, const char* entry_point,
                        const cpipe_buffer_t* const* inputs, size_t n_in,
                        cpipe_buffer_t* const* outputs, size_t n_out, const void* push_constants,
                        size_t pc_size);
    int (*request_scratch)(cpipe_compute_t*, uint64_t bytes, int kind, cpipe_buffer_t** out);
    void (*record_marker)(cpipe_compute_t*, const char* label);
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
};

typedef struct {
    cpipe_compute_t* compute;
    cpipe_inference_t* inference;
    const cpipe_buffer_t** inputs;
    size_t n_in;
    cpipe_buffer_t** outputs;
    size_t n_out;
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
