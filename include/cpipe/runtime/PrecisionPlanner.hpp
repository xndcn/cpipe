// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

namespace cpipe::runtime {

struct PrecisionEdge {
    const cpipe_plugin_desc_t* from{nullptr};
    std::string from_port;
    const cpipe_plugin_desc_t* to{nullptr};
    std::string to_port;
};

struct PrecisionGraphNode {
    std::string id;
    const cpipe_plugin_desc_t* descriptor{nullptr};
    nlohmann::json params = nlohmann::json::object();
    bool implicit{false};
};

struct PrecisionGraphEdge {
    std::size_t from{0};
    std::size_t to{0};
    std::string from_port;
    std::string to_port;
};

struct PrecisionPlan {
    std::vector<PrecisionGraphNode> nodes;
    std::vector<PrecisionGraphEdge> edges;
    std::vector<std::size_t> inserted_node_indices;
};

class PrecisionPlanner {
public:
    [[nodiscard]] static cpipe_status_t validate(std::span<const PrecisionEdge> edges,
                                                 std::string* error);
    [[nodiscard]] static cpipe_status_t auto_insert(std::span<const PrecisionGraphNode> nodes,
                                                    std::span<const PrecisionGraphEdge> edges,
                                                    const cpipe_plugin_desc_t* precision_convert,
                                                    PrecisionPlan* out, std::string* error);
};

}  // namespace cpipe::runtime
