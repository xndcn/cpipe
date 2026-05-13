// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MemoryPlanner.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/PrecisionPlanner.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);

extern const char PIPELINE_SCHEMA_JSON[];

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

struct Endpoint {
    std::string node_id;
    std::string port;
};

struct GraphEdge {
    std::size_t from{0};
    std::size_t to{0};
    std::string from_port;
    std::string to_port;
};

Endpoint split_endpoint(const std::string& endpoint) {
    const auto dot = endpoint.find('.');
    if (dot == std::string::npos) {
        return Endpoint{.node_id = endpoint, .port = {}};
    }
    return Endpoint{.node_id = endpoint.substr(0, dot), .port = endpoint.substr(dot + 1)};
}

BufferLayout layout_from_json(const nlohmann::json& input) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    const auto format = input.at("format").get<std::string>();
    layout.format = format == "R16_UINT" ? PixelFormat::R16_UINT : PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = input.value("width", 0U);
    layout.dims[1] = input.value("height", 0U);
    return layout;
}

cpipe_status_t validate_pipeline_schema(const nlohmann::json& document, std::string* error) {
    try {
        if (document.value("version", "") != "0.2") {
            set_error(error, "schema version mismatch: expected pipeline version 0.2");
            return CPIPE_FAILED;
        }
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(nlohmann::json::parse(PIPELINE_SCHEMA_JSON));
        validator.validate(document);
    } catch (const std::exception& e) {
        set_error(error, std::string{"pipeline schema validation failed: "} + e.what());
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

std::vector<std::string> manifest_metadata_steps(const cpipe_plugin_desc_t* desc,
                                                 std::string_view port_kind,
                                                 const std::string& port_name,
                                                 std::string_view field) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return {};
    }

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    for (const auto& port : manifest.value("ports", nlohmann::json::array())) {
        if (port.value("kind", "") != port_kind || port.value("name", "") != port_name) {
            continue;
        }
        const auto metadata = port.find("metadata");
        if (metadata == port.end()) {
            return {};
        }
        return metadata->value(std::string{field}, std::vector<std::string>{});
    }
    return {};
}

bool contains_step(const std::vector<std::string>& steps, const std::string& step) {
    return std::find(steps.begin(), steps.end(), step) != steps.end();
}

}  // namespace

namespace cpipe::runtime {

cpipe_status_t Pipeline::load(const std::filesystem::path& path, const Registry& registry,
                              Pipeline* out, std::string* error) {
    if (out == nullptr) {
        set_error(error, "output pipeline pointer is null");
        return CPIPE_BAD_INDEX;
    }

    std::ifstream input{path};
    if (!input) {
        set_error(error, "failed to open pipeline JSON");
        return CPIPE_FAILED;
    }

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(input);
    } catch (const std::exception& e) {
        set_error(error, std::string{"failed to parse pipeline JSON: "} + e.what());
        return CPIPE_FAILED;
    }

    if (const auto status = validate_pipeline_schema(document, error); status != CPIPE_OK) {
        return status;
    }

    std::vector<NodeInstance> nodes;
    std::vector<const cpipe_plugin_desc_t*> node_descriptors;
    std::unordered_map<std::string, std::size_t> node_index;
    const auto& json_nodes = document.at("nodes");
    nodes.reserve(json_nodes.size());
    node_descriptors.reserve(json_nodes.size());
    for (const auto& node : json_nodes) {
        const auto id = node.at("id").get<std::string>();
        const auto type = node.at("type").get<std::string>();
        const auto* desc = registry.find(type);
        if (desc == nullptr) {
            set_error(error, "unknown node type: " + type);
            return CPIPE_BAD_INDEX;
        }
        node_index.emplace(id, nodes.size());
        nodes.push_back(NodeInstance{.id = id, .descriptor = desc});
        node_descriptors.push_back(desc);
    }

    std::vector<std::vector<std::size_t>> adjacency(nodes.size());
    std::vector<std::size_t> indegree(nodes.size(), 0);
    std::vector<GraphEdge> graph_edges;
    std::vector<PrecisionEdge> precision_edges;
    for (const auto& edge : document.at("edges")) {
        const auto from_endpoint = split_endpoint(edge.at("from").get<std::string>());
        const auto to_endpoint = split_endpoint(edge.at("to").get<std::string>());
        const auto from = node_index.find(from_endpoint.node_id);
        const auto to = node_index.find(to_endpoint.node_id);
        if (from == node_index.end() || to == node_index.end()) {
            set_error(error, "dangling edge in pipeline");
            return CPIPE_BAD_INDEX;
        }
        adjacency[from->second].push_back(to->second);
        ++indegree[to->second];
        graph_edges.push_back(GraphEdge{.from = from->second,
                                        .to = to->second,
                                        .from_port = from_endpoint.port,
                                        .to_port = to_endpoint.port});
        precision_edges.push_back(PrecisionEdge{.from = nodes[from->second].descriptor,
                                                .from_port = from_endpoint.port,
                                                .to = nodes[to->second].descriptor,
                                                .to_port = to_endpoint.port});
    }

    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<NodeInstance> sorted;
    sorted.reserve(nodes.size());
    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop();
        sorted.push_back(nodes[current]);
        for (const auto next : adjacency[current]) {
            --indegree[next];
            if (indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (sorted.size() != nodes.size()) {
        set_error(error, "cycle in pipeline graph");
        return CPIPE_FAILED;
    }

    for (const auto& edge : graph_edges) {
        const auto produced = manifest_metadata_steps(nodes[edge.from].descriptor, "out",
                                                      edge.from_port, "sets_steps_applied");
        const auto required = manifest_metadata_steps(nodes[edge.to].descriptor, "in", edge.to_port,
                                                      "requires_steps_applied");
        for (const auto& step : required) {
            if (!contains_step(produced, step)) {
                set_error(error,
                          "metadata step required by pipeline edge is not produced: " + step);
                return CPIPE_NEED_METADATA;
            }
        }
    }

    if (const auto status = PrecisionPlanner::validate(precision_edges, error);
        status != CPIPE_OK) {
        return status;
    }

    std::vector<InputPort> inputs;
    inputs.reserve(document.at("inputs").size());
    for (const auto& input_json : document.at("inputs")) {
        inputs.push_back(InputPort{.name = input_json.at("port").get<std::string>(),
                                   .layout = layout_from_json(input_json)});
    }
    const auto primary_layout = inputs.empty() ? BufferLayout{} : inputs.front().layout;
    const auto memory_plan = MemoryPlanner::plan(primary_layout, node_descriptors);
    if (memory_plan.peak_bytes > out->device_memory_cap_bytes_) {
        set_error(error, "memory peak exceeds device cap");
        return CPIPE_OOM;
    }

    out->inputs_ = std::move(inputs);
    out->layout_ = primary_layout;
    out->nodes_ = std::move(sorted);
    out->sources_.clear();
    out->memory_peak_bytes_ = memory_plan.peak_bytes;
    return CPIPE_OK;
}

cpipe_status_t Pipeline::set_source(std::string port_name, std::string plugin_id,
                                    nlohmann::json params) {
    const auto found = std::find_if(inputs_.begin(), inputs_.end(),
                                    [&](const auto& input) { return input.name == port_name; });
    if (found == inputs_.end()) {
        return CPIPE_BAD_INDEX;
    }
    sources_[std::move(port_name)] =
        SourceBinding{.plugin_id = std::move(plugin_id), .params = std::move(params)};
    return CPIPE_OK;
}

cpipe_status_t Pipeline::run(std::string* error) const {
    for (const auto& input : inputs_) {
        if (sources_.find(input.name) == sources_.end()) {
            set_error(error, "source not bound for input port: " + input.name);
            return CPIPE_FAILED;
        }
    }
    set_error(error, "source plugin execution is not implemented until P1 T5");
    return CPIPE_UNSUPPORTED;
}

cpipe_status_t Pipeline::run_file(const std::filesystem::path& input_path,
                                  const std::filesystem::path& output_path,
                                  std::string* error) const {
    std::ifstream input_file{input_path, std::ios::binary};
    if (!input_file) {
        set_error(error, "failed to open input file");
        return CPIPE_FAILED;
    }

    std::vector<char> bytes{std::istreambuf_iterator<char>{input_file},
                            std::istreambuf_iterator<char>{}};
    if (bytes.size() != layout_.size_bytes()) {
        set_error(error, "input size does not match pipeline layout");
        return CPIPE_FAILED;
    }

    auto current = std::make_shared<CpuBuffer>(
        layout_, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto* current_bytes = static_cast<std::byte*>(current->lock_cpu(IBuffer::CpuAccess::Write));
    std::memcpy(current_bytes, bytes.data(), bytes.size());
    current->unlock_cpu();
    current->flush_cpu_writes();

    ComputeContext compute;
    compute.register_halide_filter("passthrough_copy", &passthrough_copy);
    HostContext host_context;

    for (const auto& node : nodes_) {
        auto next = std::make_shared<CpuBuffer>(
            layout_, BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
        void* instance = nullptr;
        auto status = static_cast<cpipe_status_t>(node.descriptor->main_entry(
            CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr, &instance));
        if (status != CPIPE_OK) {
            set_error(error, "node create failed: " + node.id);
            return status;
        }

        auto input_handle = make_buffer_handle(current);
        auto output_handle = make_buffer_handle(next);
        std::vector<std::shared_ptr<const compute::BufferMetadata>> input_metadata{
            current->metadata()};
        auto output_metadata_builder =
            make_metadata_builder_handle(current->metadata(), std::move(input_metadata));
        const cpipe_buffer_t* process_inputs[] = {input_handle.get()};
        cpipe_buffer_t* process_outputs[] = {output_handle.get()};
        cpipe_metadata_builder_t* process_out_metadata[] = {output_metadata_builder.get()};
        cpipe_process_ctx process{
            .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
            .inference = nullptr,
            .inputs = process_inputs,
            .n_in = 1,
            .outputs = process_outputs,
            .n_out = 1,
            .out_metadata = process_out_metadata,
        };
        status = static_cast<cpipe_status_t>(node.descriptor->main_entry(
            CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
            nullptr, &process, nullptr));
        next->set_metadata(freeze_metadata_builder(output_metadata_builder.get()));

        const auto destroy_status = static_cast<cpipe_status_t>(node.descriptor->main_entry(
            CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance, nullptr));
        if (status != CPIPE_OK) {
            set_error(error, "node process failed: " + node.id);
            return status;
        }
        if (destroy_status != CPIPE_OK) {
            set_error(error, "node destroy failed: " + node.id);
            return destroy_status;
        }
        current = std::move(next);
    }

    std::ofstream output_file{output_path, std::ios::binary};
    if (!output_file) {
        set_error(error, "failed to open output file");
        return CPIPE_FAILED;
    }
    const auto* output_bytes =
        static_cast<const char*>(current->lock_cpu(IBuffer::CpuAccess::Read));
    output_file.write(output_bytes, static_cast<std::streamsize>(current->size_bytes()));
    current->unlock_cpu();
    if (!output_file.good()) {
        set_error(error, "failed to write output file");
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

void Pipeline::set_device_memory_cap(std::uint64_t bytes) noexcept {
    device_memory_cap_bytes_ = bytes;
}

std::size_t Pipeline::node_count() const noexcept {
    return nodes_.size();
}

const compute::BufferLayout& Pipeline::layout() const noexcept {
    return layout_;
}

std::uint64_t Pipeline::memory_peak_bytes() const noexcept {
    return memory_peak_bytes_;
}

}  // namespace cpipe::runtime
