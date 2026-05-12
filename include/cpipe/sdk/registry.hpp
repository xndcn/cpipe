// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/sdk/sdk.hpp>
#include <cpipe/sdk/section.hpp>

// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses,modernize-use-trailing-return-type)

#define CPIPE_DETAIL_JOIN_INNER(lhs, rhs) lhs##rhs
#define CPIPE_DETAIL_JOIN(lhs, rhs) CPIPE_DETAIL_JOIN_INNER(lhs, rhs)

#define CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, id)                                 \
    namespace {                                                                                    \
    extern "C" int CPIPE_DETAIL_JOIN(cpipe_node_main_entry_,                                       \
                                     id)(const char* action, cpipe_host_t* host,                   \
                                         cpipe_node_t* node, cpipe_props_t* params, void* in_ctx,  \
                                         void* out_ctx) {                                          \
        return ::cpipe::sdk::detail::dispatch<klass>(action, host, node, params, in_ctx, out_ctx); \
    }                                                                                              \
    CPIPE_SECTION_PUT(CPIPE_REGISTRY_SECTION_NAME)                                                 \
    static const cpipe_plugin_desc_t CPIPE_DETAIL_JOIN(cpipe_node_desc_, id) = {                   \
        CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR,       klass::ID,                                         \
        klass::VERSION,  manifest_json_literal, &CPIPE_DETAIL_JOIN(cpipe_node_main_entry_, id)};   \
    }

#define CPIPE_REGISTER_NODE(klass, manifest_json_literal) \
    CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, __COUNTER__)

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses,modernize-use-trailing-return-type)
