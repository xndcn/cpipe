// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include "cpipe/sdk/cpipe_node.h"
#include "cpipe/sdk/section.hpp"

#define CPIPE_CONCAT_INNER(lhs, rhs) lhs##rhs
#define CPIPE_CONCAT(lhs, rhs) CPIPE_CONCAT_INNER(lhs, rhs)

#define CPIPE_REGISTER_NODE(desc)                                             \
    extern "C" CPIPE_REGISTRY_SECTION const cpipe_plugin_desc_t CPIPE_CONCAT( \
        cpipe_registered_node_, __COUNTER__) = desc
