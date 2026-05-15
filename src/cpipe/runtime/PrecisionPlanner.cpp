// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cpipe/runtime/PrecisionPlanner.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace cpipe::runtime {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::vector<std::string> port_precisions(const cpipe_plugin_desc_t* desc,
                                         std::string_view port_kind, const std::string& port_name) {
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

struct PortCaps {
    std::vector<std::string> channels;
    std::vector<std::string> precisions;
};

PortCaps port_caps(const cpipe_plugin_desc_t* desc, std::string_view port_kind,
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
        return PortCaps{.channels = caps->value("channels", std::vector<std::string>{}),
                        .precisions = caps->value("precision", std::vector<std::string>{})};
    }
    return {};
}

std::optional<std::string> format_from_caps(const PortCaps& caps) {
    if (caps.channels == std::vector<std::string>{"r"} &&
        caps.precisions == std::vector<std::string>{"u16"}) {
        return "R16_UINT";
    }
    if (caps.channels == std::vector<std::string>{"r"} &&
        caps.precisions == std::vector<std::string>{"f32"}) {
        return "R32_SFLOAT";
    }
    if (caps.channels == std::vector<std::string>{"rgba"} &&
        caps.precisions == std::vector<std::string>{"f16"}) {
        return "R16G16B16A16_SFLOAT";
    }
    if (caps.channels == std::vector<std::string>{"rgba"} &&
        caps.precisions == std::vector<std::string>{"u8"}) {
        return "R8G8B8A8_UNORM";
    }
    if (caps.channels == std::vector<std::string>{"rgba"} &&
        caps.precisions == std::vector<std::string>{"u16"}) {
        return "R16G16B16A16_UNORM";
    }
    return std::nullopt;
}

bool direct_conversion_supported(const std::string& from, const std::string& to) {
    return (from == "R16_UINT" && to == "R32_SFLOAT") ||
           (from == "R32_SFLOAT" && to == "R16G16B16A16_SFLOAT") ||
           (from == "R16G16B16A16_SFLOAT" && to == "R8G8B8A8_UNORM") ||
           (from == "R16G16B16A16_SFLOAT" && to == "R16G16B16A16_UNORM");
}

bool intersects(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return true;
    }
    return std::any_of(lhs.begin(), lhs.end(), [&](const std::string& item) {
        return std::find(rhs.begin(), rhs.end(), item) != rhs.end();
    });
}

PrecisionGraphEdge edge_between(std::size_t from, std::size_t to, std::string from_port,
                                std::string to_port) {
    return PrecisionGraphEdge{
        .from = from, .to = to, .from_port = std::move(from_port), .to_port = std::move(to_port)};
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

cpipe_status_t PrecisionPlanner::auto_insert(std::span<const PrecisionGraphNode> nodes,
                                             std::span<const PrecisionGraphEdge> edges,
                                             const cpipe_plugin_desc_t* precision_convert,
                                             PrecisionPlan* out, std::string* error) {
    CPIPE_TRACE_SCOPE("PrecisionPlanner::auto_insert");

    if (out == nullptr) {
        set_error(error, "precision plan output pointer is null");
        return CPIPE_BAD_INDEX;
    }

    out->nodes.assign(nodes.begin(), nodes.end());
    out->edges.clear();
    out->inserted_node_indices.clear();

    for (const auto& edge : edges) {
        if (edge.from >= nodes.size() || edge.to >= nodes.size()) {
            set_error(error, "precision edge index out of range");
            return CPIPE_BAD_INDEX;
        }

        const auto produced = port_caps(nodes[edge.from].descriptor, "out", edge.from_port);
        const auto required = port_caps(nodes[edge.to].descriptor, "in", edge.to_port);
        if (intersects(produced.precisions, required.precisions)) {
            out->edges.push_back(edge);
            continue;
        }

        const auto from_format = format_from_caps(produced);
        const auto to_format = format_from_caps(required);
        if (!from_format || !to_format || !direct_conversion_supported(*from_format, *to_format)) {
            set_error(error, "precision mismatch on pipeline edge");
            return CPIPE_BAD_PRECISION;
        }
        if (precision_convert == nullptr) {
            set_error(error, "precision_convert node unavailable");
            return CPIPE_BAD_PRECISION;
        }

        const auto inserted_index = out->nodes.size();
        const auto inserted_sequence = out->inserted_node_indices.size();
        out->nodes.push_back(PrecisionGraphNode{
            .id = "__precision_convert_" + std::to_string(edge.from) + "_" +
                  std::to_string(edge.to) + "_" + std::to_string(inserted_sequence),
            .descriptor = precision_convert,
            .params = nlohmann::json{{"target_format", *to_format}},
            .implicit = true,
        });
        out->inserted_node_indices.push_back(inserted_index);
        out->edges.push_back(edge_between(edge.from, inserted_index, edge.from_port, "in"));
        out->edges.push_back(edge_between(inserted_index, edge.to, "out", edge.to_port));
        spdlog::info("event=precision_auto_insert node={} from={} to={} target_format={}",
                     out->nodes.back().id, edge.from, edge.to, *to_format);
    }

    return CPIPE_OK;
}

}  // namespace cpipe::runtime
