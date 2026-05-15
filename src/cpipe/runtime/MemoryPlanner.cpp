// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/runtime/MemoryPlanner.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <queue>
#include <vector>

namespace cpipe::runtime {
namespace {

std::uint64_t pixel_count(const compute::BufferLayout& layout) noexcept {
    if (layout.ndim < 2) {
        return 0;
    }
    return static_cast<std::uint64_t>(layout.dims[0]) * layout.dims[1];
}

std::uint64_t output_bytes_per_pixel(const cpipe_plugin_desc_t* desc) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return 0;
    }
    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    return manifest.value("compute", nlohmann::json::object())
        .value("out_pixel_bytes", std::uint64_t{0});
}

std::vector<std::size_t> topological_order(std::size_t node_count,
                                           std::span<const MemoryGraphEdge> edges) {
    std::vector<std::vector<std::size_t>> adjacency(node_count);
    std::vector<std::size_t> indegree(node_count, 0);
    for (const auto& edge : edges) {
        if (edge.from >= node_count || edge.to >= node_count) {
            continue;
        }
        adjacency[edge.from].push_back(edge.to);
        ++indegree[edge.to];
    }

    std::queue<std::size_t> ready;
    for (std::size_t node = 0; node < node_count; ++node) {
        if (indegree[node] == 0) {
            ready.push(node);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(node_count);
    while (!ready.empty()) {
        const auto node = ready.front();
        ready.pop();
        order.push_back(node);
        for (const auto next : adjacency[node]) {
            --indegree[next];
            if (indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (order.size() != node_count) {
        order.resize(node_count);
        for (std::size_t node = 0; node < node_count; ++node) {
            order[node] = node;
        }
    }
    return order;
}

struct LiveBuffer {
    std::size_t node_index{0};
    std::size_t start{0};
    std::size_t end{0};
    std::uint64_t size_bytes{0};
    std::size_t physical_slot{0};
};

using InterferenceGraph = std::vector<std::vector<bool>>;

bool overlaps(const LiveBuffer& lhs, const LiveBuffer& rhs) noexcept {
    return lhs.start < rhs.end && rhs.start < lhs.end;
}

std::vector<std::size_t> topological_positions(std::size_t node_count,
                                               const std::vector<std::size_t>& order) {
    std::vector<std::size_t> positions(node_count, 0);
    for (std::size_t position = 0; position < order.size(); ++position) {
        positions[order[position]] = position;
    }
    return positions;
}

std::vector<LiveBuffer> make_live_buffers(std::span<const cpipe_plugin_desc_t* const> nodes,
                                          const std::vector<std::size_t>& order,
                                          std::uint64_t pixels) {
    std::vector<LiveBuffer> buffers(nodes.size());
    for (std::size_t position = 0; position < order.size(); ++position) {
        const auto node = order[position];
        buffers[node] = LiveBuffer{.node_index = node,
                                   .start = position,
                                   .end = position + 1,
                                   .size_bytes = pixels * output_bytes_per_pixel(nodes[node])};
    }
    return buffers;
}

void extend_live_ranges(std::vector<LiveBuffer>* buffers,
                        const std::vector<std::size_t>& topo_position,
                        std::span<const MemoryGraphEdge> edges) {
    for (const auto& edge : edges) {
        if (edge.from >= buffers->size() || edge.to >= buffers->size()) {
            continue;
        }
        auto& buffer = (*buffers)[edge.from];
        buffer.end = std::max(buffer.end, topo_position[edge.to]);
    }
}

InterferenceGraph build_interference_graph(const std::vector<LiveBuffer>& buffers) {
    InterferenceGraph interference(buffers.size(), std::vector<bool>(buffers.size()));
    for (std::size_t lhs = 0; lhs < buffers.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < buffers.size(); ++rhs) {
            if (overlaps(buffers[lhs], buffers[rhs])) {
                interference[lhs][rhs] = true;
                interference[rhs][lhs] = true;
            }
        }
    }
    return interference;
}

bool slot_available(const std::vector<LiveBuffer>& assigned, const InterferenceGraph& interference,
                    const LiveBuffer& current, std::size_t slot) {
    return std::ranges::none_of(assigned, [&](const auto& other) {
        return other.physical_slot == slot && interference[current.node_index][other.node_index];
    });
}

std::vector<LiveBuffer> color_buffers(const std::vector<LiveBuffer>& buffers,
                                      const std::vector<std::size_t>& order,
                                      const InterferenceGraph& interference,
                                      std::vector<std::uint64_t>* slot_sizes) {
    std::vector<LiveBuffer> assigned;
    assigned.reserve(buffers.size());
    for (const auto node : order) {
        auto current = buffers[node];
        for (std::size_t slot = 0; slot <= slot_sizes->size(); ++slot) {
            if (!slot_available(assigned, interference, current, slot)) {
                continue;
            }
            current.physical_slot = slot;
            if (slot == slot_sizes->size()) {
                slot_sizes->push_back(current.size_bytes);
            } else {
                (*slot_sizes)[slot] = std::max((*slot_sizes)[slot], current.size_bytes);
            }
            assigned.push_back(current);
            break;
        }
    }
    return assigned;
}

MemoryPlan make_plan(std::uint64_t input_bytes, const std::vector<std::uint64_t>& slot_sizes,
                     const std::vector<LiveBuffer>& assigned) {
    std::uint64_t peak = input_bytes;
    for (const auto slot_size : slot_sizes) {
        peak += slot_size;
    }

    MemoryPlan plan{
        .peak_bytes = peak, .physical_slot_count = slot_sizes.size(), .assignments = {}};
    plan.assignments.reserve(assigned.size());
    for (const auto& buffer : assigned) {
        plan.assignments.push_back(MemoryAssignment{.buffer_id = buffer.node_index,
                                                    .node_index = buffer.node_index,
                                                    .physical_slot = buffer.physical_slot,
                                                    .size_bytes = buffer.size_bytes});
    }
    return plan;
}

}  // namespace

MemoryPlan MemoryPlanner::plan(compute::BufferLayout input_layout,
                               std::span<const cpipe_plugin_desc_t* const> nodes) {
    std::vector<MemoryGraphEdge> edges;
    edges.reserve(nodes.empty() ? 0 : nodes.size() - 1);
    for (std::size_t node = 1; node < nodes.size(); ++node) {
        edges.push_back(MemoryGraphEdge{.from = node - 1, .to = node});
    }
    return plan_graph(input_layout, nodes, edges);
}

MemoryPlan MemoryPlanner::plan_graph(compute::BufferLayout input_layout,
                                     std::span<const cpipe_plugin_desc_t* const> nodes,
                                     std::span<const MemoryGraphEdge> edges) {
    CPIPE_TRACE_SCOPE("MemoryPlanner::plan_graph_coloring");

    const auto input_bytes = input_layout.size_bytes();
    const auto pixels = pixel_count(input_layout);
    if (nodes.empty()) {
        return MemoryPlan{.peak_bytes = input_bytes, .physical_slot_count = 0, .assignments = {}};
    }

    const auto order = topological_order(nodes.size(), edges);
    const auto topo_position = topological_positions(nodes.size(), order);
    auto buffers = make_live_buffers(nodes, order, pixels);
    extend_live_ranges(&buffers, topo_position, edges);
    const auto interference = build_interference_graph(buffers);
    std::vector<std::uint64_t> slot_sizes;
    const auto assigned = color_buffers(buffers, order, interference, &slot_sizes);
    return make_plan(input_bytes, slot_sizes, assigned);
}

}  // namespace cpipe::runtime
