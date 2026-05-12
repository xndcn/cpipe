// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/sdk/cpipe_node.h"

typedef char cpipe_abi_major_is_zero[(CPIPE_ABI_MAJOR == 0) ? 1 : -1];
typedef char cpipe_abi_minor_is_one[(CPIPE_ABI_MINOR == 1) ? 1 : -1];
typedef char cpipe_unsupported_value_is_stable[(CPIPE_UNSUPPORTED == 8) ? 1 : -1];

static int cpipe_test_submit_inference(cpipe_inference_t* inference, const char* model_id,
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

int cpipe_c_abi_compile_probe(void) {
    cpipe_inference_suite_v1 suite = {&cpipe_test_submit_inference};
    cpipe_plugin_desc_t desc = {
        CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, "com.cpipe.test.c-abi", "1.0.0", "{}", 0};

    (void)desc;
    return suite.submit_inference(0, 0, 0, 0, 0, 0);
}
