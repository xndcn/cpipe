// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <string_view>

#include "cpipe/sdk/registry.hpp"

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);

namespace cpipe::nodes {

extern const char kPassthroughManifestJson[];

namespace {

extern "C" __attribute__((used)) int (*const cpipe_passthrough_copy_link_anchor)(
    halide_buffer_t*, halide_buffer_t*) = &passthrough_copy;

int passthrough_main(const char* action, cpipe_host_t* host, cpipe_node_t*, cpipe_props_t*,
                     void* in_ctx, void* out_ctx) {
    if (action == nullptr) {
        return CPIPE_FAILED;
    }
    if (std::string_view(action) == CPIPE_ACTION_CREATE) {
        if (out_ctx != nullptr) {
            *static_cast<void**>(out_ctx) = nullptr;
        }
        return CPIPE_OK;
    }
    if (std::string_view(action) == CPIPE_ACTION_DESTROY ||
        std::string_view(action) == CPIPE_ACTION_PREPARE ||
        std::string_view(action) == CPIPE_ACTION_DESCRIBE) {
        return CPIPE_OK;
    }
    if (std::string_view(action) != CPIPE_ACTION_PROCESS) {
        return CPIPE_REPLY_DEFAULT;
    }
    if (host == nullptr || in_ctx == nullptr) {
        return CPIPE_FAILED;
    }

    const auto* compute =
        static_cast<const cpipe_compute_suite_v1*>(host->get_suite(host, "compute", 1));
    if (compute == nullptr) {
        return CPIPE_UNSUPPORTED;
    }

    auto* process = static_cast<cpipe_process_ctx*>(in_ctx);
    return compute->submit_halide(process->compute, "passthrough_copy", process->inputs,
                                  process->n_in, process->outputs, process->n_out);
}

const cpipe_plugin_desc_t kPassthroughDesc{
    CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR,          "com.cpipe.builtin.passthrough",
    "0.1.0",         kPassthroughManifestJson, passthrough_main,
};

}  // namespace

}  // namespace cpipe::nodes

CPIPE_REGISTER_NODE(cpipe::nodes::kPassthroughDesc);
