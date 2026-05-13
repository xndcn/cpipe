// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <span>
#include <string>

namespace cpipe::runtime {

struct PrecisionEdge {
    const cpipe_plugin_desc_t* from{nullptr};
    std::string from_port;
    const cpipe_plugin_desc_t* to{nullptr};
    std::string to_port;
};

class PrecisionPlanner {
public:
    [[nodiscard]] static cpipe_status_t validate(std::span<const PrecisionEdge> edges,
                                                 std::string* error);
};

}  // namespace cpipe::runtime
