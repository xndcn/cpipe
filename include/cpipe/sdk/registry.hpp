// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/sdk.hpp>
#include <cpipe/sdk/section.hpp>

#define CPIPE_DETAIL_CONCAT_INNER(lhs, rhs) lhs##rhs
#define CPIPE_DETAIL_CONCAT(lhs, rhs) CPIPE_DETAIL_CONCAT_INNER(lhs, rhs)

#define CPIPE_REGISTER_NODE(klass, manifest_json_literal) \
    CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, __COUNTER__)

#define CPIPE_REGISTER_NODE_IMPL(klass, manifest_json_literal, counter)                 \
    namespace {                                                                         \
    CPIPE_SECTION_PUT const cpipe_plugin_desc_t CPIPE_DETAIL_CONCAT(cpipe_plugin_desc_, \
                                                                    counter) = {        \
        CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR,       klass::ID,                              \
        klass::VERSION,  manifest_json_literal, &::cpipe::sdk::detail::dispatch<klass>, \
    };                                                                                  \
    }
