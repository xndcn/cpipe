// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

enum {
    cpipe_abi_major_is_zero = 1 / (CPIPE_ABI_MAJOR == 0),
    cpipe_abi_minor_is_one = 1 / (CPIPE_ABI_MINOR == 1),
    cpipe_ok_is_zero = 1 / (CPIPE_OK == 0),
    cpipe_unsupported_is_eight = 1 / (CPIPE_UNSUPPORTED == 8)
};

static int get_dims(const cpipe_buffer_t* buffer, uint8_t* ndim, uint32_t out_dims[8]) {
    (void)buffer;
    out_dims[0] = 1;
    *ndim = 1;
    return CPIPE_OK;
}

static int get_format(const cpipe_buffer_t* buffer, int* out) {
    (void)buffer;
    *out = 0;
    return CPIPE_OK;
}

static int get_kind(const cpipe_buffer_t* buffer, int* out) {
    (void)buffer;
    *out = 0;
    return CPIPE_OK;
}

static int get_stride(const cpipe_buffer_t* buffer, uint64_t out_stride[8]) {
    (void)buffer;
    out_stride[0] = 1;
    return CPIPE_OK;
}

static int get_color_role(const cpipe_buffer_t* buffer, const char** out) {
    (void)buffer;
    *out = "scene_linear";
    return CPIPE_OK;
}

static int lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr) {
    (void)buffer;
    (void)access;
    *ptr = 0;
    return CPIPE_UNSUPPORTED;
}

static int buffer_no_arg(cpipe_buffer_t* buffer) {
    (void)buffer;
    return CPIPE_OK;
}

static int submit_halide(cpipe_compute_t* compute, const char* aot_id,
                         const cpipe_buffer_t* const* inputs, size_t n_in,
                         cpipe_buffer_t* const* outputs, size_t n_out) {
    (void)compute;
    (void)aot_id;
    (void)inputs;
    (void)n_in;
    (void)outputs;
    (void)n_out;
    return CPIPE_UNSUPPORTED;
}

static int submit_slang(cpipe_compute_t* compute, const char* module_id, const char* entry_point,
                        const cpipe_buffer_t* const* inputs, size_t n_in,
                        cpipe_buffer_t* const* outputs, size_t n_out, const void* push_constants,
                        size_t pc_size) {
    (void)compute;
    (void)module_id;
    (void)entry_point;
    (void)inputs;
    (void)n_in;
    (void)outputs;
    (void)n_out;
    (void)push_constants;
    (void)pc_size;
    return CPIPE_UNSUPPORTED;
}

static int request_scratch(cpipe_compute_t* compute, uint64_t bytes, int kind,
                           cpipe_buffer_t** out) {
    (void)compute;
    (void)bytes;
    (void)kind;
    *out = 0;
    return CPIPE_UNSUPPORTED;
}

static void record_marker(cpipe_compute_t* compute, const char* label) {
    (void)compute;
    (void)label;
}

static int submit_inference(cpipe_inference_t* inference, const char* model_id,
                            const cpipe_buffer_t* const* inputs, size_t n_in,
                            cpipe_buffer_t* const* outputs, size_t n_out) {
    (void)inference;
    (void)model_id;
    (void)inputs;
    (void)n_in;
    (void)outputs;
    (void)n_out;
    return CPIPE_UNSUPPORTED;
}

static int get_double(const cpipe_props_t* props, const char* key, double* out) {
    (void)props;
    (void)key;
    *out = 0.0;
    return CPIPE_OK;
}

static int get_int(const cpipe_props_t* props, const char* key, int64_t* out) {
    (void)props;
    (void)key;
    *out = 0;
    return CPIPE_OK;
}

static int get_bool(const cpipe_props_t* props, const char* key, int* out) {
    (void)props;
    (void)key;
    *out = 0;
    return CPIPE_OK;
}

static int get_enum(const cpipe_props_t* props, const char* key, const char** out) {
    (void)props;
    (void)key;
    *out = "value";
    return CPIPE_OK;
}

static int get_curve(const cpipe_props_t* props, const char* key, const float** xs,
                     const float** ys, size_t* n) {
    (void)props;
    (void)key;
    *xs = 0;
    *ys = 0;
    *n = 0;
    return CPIPE_OK;
}

static int get_color(const cpipe_props_t* props, const char* key, float rgba[4]) {
    (void)props;
    (void)key;
    rgba[0] = 1.0F;
    rgba[1] = 1.0F;
    rgba[2] = 1.0F;
    rgba[3] = 1.0F;
    return CPIPE_OK;
}

static const void* get_suite(cpipe_host_t* host, const char* suite_name, int version) {
    (void)host;
    (void)suite_name;
    (void)version;
    return 0;
}

static void log_message(cpipe_host_t* host, int level, const char* msg) {
    (void)host;
    (void)level;
    (void)msg;
}

static void* allocate(cpipe_host_t* host, size_t bytes) {
    (void)host;
    (void)bytes;
    return 0;
}

static void deallocate(cpipe_host_t* host, void* ptr) {
    (void)host;
    (void)ptr;
}

static int main_entry(const char* action, cpipe_host_t* host, cpipe_node_t* node,
                      cpipe_props_t* params, void* in_ctx, void* out_ctx) {
    (void)action;
    (void)host;
    (void)node;
    (void)params;
    (void)in_ctx;
    (void)out_ctx;
    return CPIPE_REPLY_DEFAULT;
}

int main(void) {
    cpipe_buffer_suite_v1 buffer_suite = {get_dims,       get_format, get_kind,      get_stride,
                                          get_color_role, lock_cpu,   buffer_no_arg, buffer_no_arg};
    cpipe_compute_suite_v1 compute_suite = {submit_halide, submit_slang, request_scratch,
                                            record_marker};
    cpipe_inference_suite_v1 inference_suite = {submit_inference};
    cpipe_param_suite_v1 param_suite = {get_double, get_int,   get_bool,
                                        get_enum,   get_curve, get_color};
    cpipe_host_t host = {CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, get_suite,
                         log_message,     allocate,        deallocate};
    cpipe_plugin_desc_t desc = {
        CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, "com.cpipe.test.c99", "1.0.0", "{}", main_entry};
    cpipe_process_ctx process_ctx = {0, 0, 0, 0, 0, 0};

    return buffer_suite.get_dims != 0 && compute_suite.submit_halide != 0 &&
                   inference_suite.submit_inference != 0 && param_suite.get_double != 0 &&
                   host.get_suite != 0 && desc.main_entry != 0 && process_ctx.n_in == 0
               ? 0
               : 1;
}
