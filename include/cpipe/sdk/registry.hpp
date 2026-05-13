// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/sdk/sdk.hpp>
#include <cpipe/sdk/section.hpp>

#define CPIPE_DETAIL_CONCAT_INNER(lhs, rhs) lhs##rhs
#define CPIPE_DETAIL_CONCAT(lhs, rhs) CPIPE_DETAIL_CONCAT_INNER(lhs, rhs)

#define CPIPE_REGISTER_NODE(klass, manifest_json_literal) \
    CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, __COUNTER__)

#define CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, counter)                            \
    namespace {                                                                                    \
    int CPIPE_DETAIL_CONCAT(cpipe_node_main_entry_,                                                \
                            counter)(const char* action, cpipe_host_t* host, cpipe_node_t* node,   \
                                     cpipe_props_t* params, void* in_ctx, void* out_ctx) {         \
        return ::cpipe::sdk::detail::dispatch<klass>(action, host, node, params, in_ctx, out_ctx); \
    }                                                                                              \
    CPIPE_SECTION_PUT const cpipe_plugin_desc_t CPIPE_DETAIL_CONCAT(cpipe_registry_desc_,          \
                                                                    counter) = {                   \
        CPIPE_ABI_MAJOR,                                                                           \
        CPIPE_ABI_MINOR,                                                                           \
        klass::ID,                                                                                 \
        klass::VERSION,                                                                            \
        manifest_json_literal,                                                                     \
        &CPIPE_DETAIL_CONCAT(cpipe_node_main_entry_, counter)};                                    \
    }
