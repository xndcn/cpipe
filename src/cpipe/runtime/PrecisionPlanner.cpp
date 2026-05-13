// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/PrecisionPlanner.hpp>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace cpipe::runtime {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::vector<std::string> port_precisions(const cpipe_plugin_desc_t* desc, std::string_view port_kind,
                                         const std::string& port_name) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return {};
    }

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    for (const auto& port : manifest.value("ports", nlohmann::json::array())) {
        if (port.value("kind", "") != port_kind || port.value("name", "") != port_name) {
            continue;
        }
        const auto caps = port.find("caps");
        if (caps == port.end()) {
            return {};
        }
        return caps->value("precision", std::vector<std::string>{});
    }
    return {};
}

bool intersects(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return true;
    }
    return std::any_of(lhs.begin(), lhs.end(), [&](const std::string& item) {
        return std::find(rhs.begin(), rhs.end(), item) != rhs.end();
    });
}

}  // namespace

cpipe_status_t PrecisionPlanner::validate(std::span<const PrecisionEdge> edges,
                                          std::string* error) {
    for (const auto& edge : edges) {
        const auto produced = port_precisions(edge.from, "out", edge.from_port);
        const auto required = port_precisions(edge.to, "in", edge.to_port);
        if (!intersects(produced, required)) {
            set_error(error, "precision mismatch on pipeline edge");
            return CPIPE_BAD_PRECISION;
        }
    }
    return CPIPE_OK;
}

}  // namespace cpipe::runtime
