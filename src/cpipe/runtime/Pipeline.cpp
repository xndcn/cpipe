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
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/PrecisionPlanner.hpp>
#include <cpipe/runtime/Scheduler.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

std::optional<PixelFormat> pixel_format_from_string(std::string_view format) {
    if (format == "R16_UINT") {
        return PixelFormat::R16_UINT;
    }
    if (format == "R32_SFLOAT") {
        return PixelFormat::R32_SFLOAT;
    }
    if (format == "R16G16B16A16_SFLOAT") {
        return PixelFormat::R16G16B16A16_SFLOAT;
    }
    if (format == "R8G8B8A8_UNORM") {
        return PixelFormat::R8G8B8A8_UNORM;
    }
    if (format == "R16G16B16A16_UNORM") {
        return PixelFormat::R16G16B16A16_UNORM;
    }
    return std::nullopt;
}

BufferLayout layout_from_json(const nlohmann::json& input) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    const auto format = input.at("format").get<std::string>();
    layout.format = pixel_format_from_string(format).value_or(PixelFormat::R8G8B8A8_UNORM);
    layout.ndim = 2;
    layout.dims[0] = input.value("width", 0U);
    layout.dims[1] = input.value("height", 0U);
    return layout;
}

std::optional<PixelFormat> manifest_pixel_format(const nlohmann::json& port) {
    const auto caps = port.find("caps");
    if (caps == port.end()) {
        return std::nullopt;
    }
    const auto channels = caps->value("channels", std::vector<std::string>{});
    const auto precision = caps->value("precision", std::vector<std::string>{});
    if (channels == std::vector<std::string>{"r"} && precision == std::vector<std::string>{"u16"}) {
        return PixelFormat::R16_UINT;
    }
    if (channels == std::vector<std::string>{"r"} && precision == std::vector<std::string>{"f32"}) {
        return PixelFormat::R32_SFLOAT;
    }
    if (channels == std::vector<std::string>{"rgba"} &&
        precision == std::vector<std::string>{"f16"}) {
        return PixelFormat::R16G16B16A16_SFLOAT;
    }
    if (channels == std::vector<std::string>{"rgba"} &&
        precision == std::vector<std::string>{"u8"}) {
        return PixelFormat::R8G8B8A8_UNORM;
    }
    if (channels == std::vector<std::string>{"rgba"} &&
        precision == std::vector<std::string>{"u16"}) {
        return PixelFormat::R16G16B16A16_UNORM;
    }
    return std::nullopt;
}

std::optional<BufferLayout> output_layout_for_node(const cpipe_plugin_desc_t* desc,
                                                   const nlohmann::json& params,
                                                   const BufferLayout& input, std::string* error) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        set_error(error, "missing node manifest");
        return std::nullopt;
    }
    if (desc->node_id != nullptr &&
        std::string_view{desc->node_id} == "com.cpipe.precision_convert") {
        const auto target = params.value("target_format", "");
        const auto format = pixel_format_from_string(target);
        if (!format) {
            set_error(error, "unsupported precision_convert target format");
            return std::nullopt;
        }
        BufferLayout layout = input;
        layout.format = *format;
        return layout;
    }

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    for (const auto& port : manifest.value("ports", nlohmann::json::array())) {
        if (port.value("kind", "") != "out") {
            continue;
        }
        auto format = manifest_pixel_format(port);
        if (!format) {
            set_error(error, "unsupported output port format");
            return std::nullopt;
        }
        BufferLayout layout = input;
        layout.format = *format;
        return layout;
    }
    return std::nullopt;
}

bool node_has_output(const cpipe_plugin_desc_t* desc) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return false;
    }
    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    return std::ranges::any_of(manifest.value("ports", nlohmann::json::array()),
                               [](const auto& port) { return port.value("kind", "") == "out"; });
}

std::size_t node_input_count(const cpipe_plugin_desc_t* desc) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return 1;
    }
    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    const auto count = static_cast<std::size_t>(
        std::ranges::count_if(manifest.value("ports", nlohmann::json::array()),
                              [](const auto& port) { return port.value("kind", "") == "in"; }));
    return std::max<std::size_t>(count, 1);
}

cpipe_status_t validate_pipeline_schema(const nlohmann::json& document, std::string* error) {
    try {
        if (document.value("version", "") != "0.3") {
            set_error(error, "schema version mismatch: expected pipeline version 0.3");
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

cpipe_status_t validate_enum_param(const std::string& name, const nlohmann::json& value,
                                   const nlohmann::json& declaration, std::string* error) {
    if (!value.is_string()) {
        set_error(error, "node param must be enum string: " + name);
        return CPIPE_BAD_INDEX;
    }
    const auto enum_values = declaration.value("enum_values", std::vector<std::string>{});
    const auto selected = value.get<std::string>();
    if (std::find(enum_values.begin(), enum_values.end(), selected) == enum_values.end()) {
        set_error(error, "node param outside enum: " + name);
        return CPIPE_BAD_INDEX;
    }
    return CPIPE_OK;
}

cpipe_status_t validate_number_param(const std::string& name, const nlohmann::json& value,
                                     const nlohmann::json& declaration, std::string* error) {
    if (!value.is_number()) {
        set_error(error, "node param must be number: " + name);
        return CPIPE_BAD_INDEX;
    }
    const auto range = declaration.find("range");
    if (range == declaration.end()) {
        return CPIPE_OK;
    }

    const auto selected = value.get<double>();
    if (range->contains("min") && selected < range->at("min").get<double>()) {
        set_error(error, "node param below range: " + name);
        return CPIPE_BAD_INDEX;
    }
    if (range->contains("max") && selected > range->at("max").get<double>()) {
        set_error(error, "node param above range: " + name);
        return CPIPE_BAD_INDEX;
    }
    return CPIPE_OK;
}

cpipe_status_t validate_typed_param(const std::string& name, const nlohmann::json& value,
                                    const nlohmann::json& declaration, std::string* error) {
    const auto type = declaration.value("type", "");
    if (type == "enum") {
        return validate_enum_param(name, value, declaration, error);
    }
    if (type == "number") {
        return validate_number_param(name, value, declaration, error);
    }
    if (type == "string" && !value.is_string()) {
        set_error(error, "node param must be string: " + name);
        return CPIPE_BAD_INDEX;
    }
    if (type == "array" && !value.is_array()) {
        set_error(error, "node param must be array: " + name);
        return CPIPE_BAD_INDEX;
    }
    if (type == "string" || type == "array") {
        return CPIPE_OK;
    }

    set_error(error, "unsupported node param type: " + name);
    return CPIPE_BAD_INDEX;
}

cpipe_status_t validate_node_params(const cpipe_plugin_desc_t* desc, const nlohmann::json& params,
                                    std::string* error) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        set_error(error, "missing node manifest");
        return CPIPE_BAD_INDEX;
    }

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    const auto declarations = manifest.value("params", nlohmann::json::array());
    for (const auto& [name, value] : params.items()) {
        const auto found = std::find_if(
            declarations.begin(), declarations.end(),
            [&](const auto& declaration) { return declaration.value("name", "") == name; });
        if (found == declarations.end()) {
            set_error(error, "unknown node param: " + name);
            return CPIPE_BAD_INDEX;
        }
        if (const auto status = validate_typed_param(name, value, *found, error);
            status != CPIPE_OK) {
            return status;
        }
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

std::string manifest_color_role(const cpipe_plugin_desc_t* desc, std::string_view field) {
    if (desc == nullptr || desc->manifest_json == nullptr) {
        return "any";
    }

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    const auto color = manifest.find("color");
    if (color == manifest.end()) {
        return "any";
    }
    return color->value(std::string{field}, "any");
}

bool contains_step(const std::vector<std::string>& steps, const std::string& step) {
    return std::find(steps.begin(), steps.end(), step) != steps.end();
}

bool color_roles_compatible(const std::string& produced, const std::string& required) {
    return produced.empty() || required.empty() || produced == "any" || required == "any" ||
           produced == required;
}

}  // namespace

namespace cpipe::runtime {

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
        if (type == "com.cpipe.precision_convert") {
            set_error(error, "reserved implicit node type: " + type);
            return CPIPE_BAD_INDEX;
        }
        const auto* desc = registry.find(type);
        if (desc == nullptr) {
            set_error(error, "unknown node type: " + type);
            return CPIPE_BAD_INDEX;
        }
        if (const auto status = validate_node_params(desc, node.at("params"), error);
            status != CPIPE_OK) {
            return status;
        }
        node_index.emplace(id, nodes.size());
        nodes.push_back(NodeInstance{.id = id, .descriptor = desc, .params = node.at("params")});
        node_descriptors.push_back(desc);
    }

    std::vector<std::vector<std::size_t>> adjacency(nodes.size());
    std::vector<std::size_t> indegree(nodes.size(), 0);
    std::vector<GraphEdge> graph_edges;
    std::vector<MemoryGraphEdge> memory_edges;
    std::vector<PrecisionGraphEdge> precision_graph_edges;
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
        memory_edges.push_back(MemoryGraphEdge{.from = from->second, .to = to->second});
        precision_graph_edges.push_back(PrecisionGraphEdge{.from = from->second,
                                                           .to = to->second,
                                                           .from_port = from_endpoint.port,
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

        const auto produced_role = manifest_color_role(nodes[edge.from].descriptor, "output_role");
        const auto required_role = manifest_color_role(nodes[edge.to].descriptor, "input_role");
        if (!color_roles_compatible(produced_role, required_role)) {
            std::string message = "color role mismatch on pipeline edge: ";
            message += produced_role;
            message += " -> ";
            message += required_role;
            set_error(error, std::move(message));
            return CPIPE_NEED_METADATA;
        }
    }

    std::vector<PrecisionGraphNode> precision_nodes;
    precision_nodes.reserve(nodes.size());
    for (const auto& node : nodes) {
        precision_nodes.push_back(PrecisionGraphNode{.id = node.id,
                                                     .descriptor = node.descriptor,
                                                     .params = node.params,
                                                     .implicit = false});
    }
    PrecisionPlan precision_plan;
    if (const auto status = PrecisionPlanner::auto_insert(
            precision_nodes, precision_graph_edges, registry.find("com.cpipe.precision_convert"),
            &precision_plan, error);
        status != CPIPE_OK) {
        return status;
    }

    node_descriptors.clear();
    node_descriptors.reserve(precision_plan.nodes.size());
    for (const auto& node : precision_plan.nodes) {
        node_descriptors.push_back(node.descriptor);
    }
    memory_edges.clear();
    memory_edges.reserve(precision_plan.edges.size());
    for (const auto& edge : precision_plan.edges) {
        memory_edges.push_back(MemoryGraphEdge{.from = edge.from, .to = edge.to});
    }

    std::vector<std::vector<std::size_t>> precision_adjacency(precision_plan.nodes.size());
    std::vector<std::size_t> precision_indegree(precision_plan.nodes.size(), 0);
    for (const auto& edge : precision_plan.edges) {
        if (edge.from >= precision_plan.nodes.size() || edge.to >= precision_plan.nodes.size()) {
            set_error(error, "precision edge index out of range");
            return CPIPE_BAD_INDEX;
        }
        precision_adjacency[edge.from].push_back(edge.to);
        ++precision_indegree[edge.to];
    }

    std::queue<std::size_t> precision_ready;
    for (std::size_t i = 0; i < precision_indegree.size(); ++i) {
        if (precision_indegree[i] == 0) {
            precision_ready.push(i);
        }
    }

    std::vector<NodeInstance> planned_nodes;
    planned_nodes.reserve(precision_plan.nodes.size());
    while (!precision_ready.empty()) {
        const auto current = precision_ready.front();
        precision_ready.pop();
        const auto& node = precision_plan.nodes[current];
        planned_nodes.push_back(
            NodeInstance{.id = node.id, .descriptor = node.descriptor, .params = node.params});
        for (const auto next : precision_adjacency[current]) {
            --precision_indegree[next];
            if (precision_indegree[next] == 0) {
                precision_ready.push(next);
            }
        }
    }
    if (planned_nodes.size() != precision_plan.nodes.size()) {
        set_error(error, "cycle in precision-planned pipeline graph");
        return CPIPE_FAILED;
    }

    std::vector<InputPort> inputs;
    inputs.reserve(document.at("inputs").size());
    for (const auto& input_json : document.at("inputs")) {
        inputs.push_back(InputPort{.name = input_json.at("port").get<std::string>(),
                                   .layout = layout_from_json(input_json)});
    }
    const auto primary_layout = inputs.empty() ? BufferLayout{} : inputs.front().layout;
    const auto memory_plan =
        MemoryPlanner::plan_graph(primary_layout, node_descriptors, memory_edges);
    if (memory_plan.peak_bytes > out->device_memory_cap_bytes_) {
        set_error(error, "memory peak exceeds device cap");
        return CPIPE_OOM;
    }

    out->inputs_ = std::move(inputs);
    out->layout_ = primary_layout;
    out->nodes_ = std::move(planned_nodes);
    out->sources_.clear();
    out->registry_ = &registry;
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
    const auto* desc = registry_ == nullptr ? nullptr : registry_->find(plugin_id);
    if (desc == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    sources_[std::move(port_name)] = SourceBinding{
        .plugin_id = std::move(plugin_id), .descriptor = desc, .params = std::move(params)};
    return CPIPE_OK;
}

cpipe_status_t Pipeline::run(std::string* error) const {
    return run_bound(std::nullopt, error);
}

cpipe_status_t Pipeline::run_to_file(const std::filesystem::path& output,
                                     std::string* error) const {
    return run_bound(output, error);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
cpipe_status_t Pipeline::run_bound(std::optional<std::filesystem::path> output,
                                   std::string* error) const {
    CPIPE_TRACE_SCOPE("Pipeline::run");

    for (const auto& input : inputs_) {
        if (sources_.find(input.name) == sources_.end()) {
            set_error(error, "source not bound for input port: " + input.name);
            return CPIPE_FAILED;
        }
    }
    if (inputs_.size() != 1) {
        set_error(error, "only one pipeline input is supported in P1");
        return CPIPE_UNSUPPORTED;
    }

    const auto source = sources_.find(inputs_.front().name);
    if (source == sources_.end() || source->second.descriptor == nullptr) {
        set_error(error, "source plugin is not bound");
        return CPIPE_FAILED;
    }

    ComputeContext compute;
    HostContext host_context;

    auto source_params = make_param_handle(source->second.params);
    auto source_output = std::make_shared<CpuBuffer>(
        inputs_.front().layout, BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto source_output_handle = make_buffer_handle(source_output);
    cpipe_buffer_t* source_outputs[] = {source_output_handle.get()};
    cpipe_process_ctx source_process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = nullptr,
        .n_in = 0,
        .outputs = source_outputs,
        .n_out = 1,
        .out_metadata = nullptr,
    };

    void* source_instance = nullptr;
    auto status = static_cast<cpipe_status_t>(
        source->second.descriptor->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr,
                                              source_params.get(), nullptr, &source_instance));
    if (status != CPIPE_OK) {
        set_error(error, "source create failed: " + source->second.plugin_id);
        return status;
    }
    status = static_cast<cpipe_status_t>(source->second.descriptor->main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(source_instance),
        source_params.get(), &source_process, nullptr));
    const auto source_destroy = static_cast<cpipe_status_t>(source->second.descriptor->main_entry(
        CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, source_instance, nullptr));
    if (status != CPIPE_OK) {
        set_error(error, "source process failed: " + source->second.plugin_id);
        return status;
    }
    if (source_destroy != CPIPE_OK) {
        set_error(error, "source destroy failed: " + source->second.plugin_id);
        return source_destroy;
    }

    auto current = buffer_from_handle(source_output_handle.get());
    if (current == nullptr) {
        set_error(error, "source produced no buffer");
        return CPIPE_FAILED;
    }

    std::vector<ScheduledNode> scheduled_nodes;
    scheduled_nodes.reserve(nodes_.size());
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        const auto* node = &nodes_[index];
        std::vector<std::size_t> dependencies;
        if (index > 0) {
            dependencies.push_back(index - 1);
        }
        scheduled_nodes.push_back(ScheduledNode{
            .id = node->id,
            .process =
                [&, node] {
                    auto params_json = node->params;
                    const bool has_output = node_has_output(node->descriptor);
                    if (!has_output && output) {
                        params_json["path"] = output->string();
                    }
                    auto params = make_param_handle(params_json);

                    void* instance = nullptr;
                    auto node_status = static_cast<cpipe_status_t>(
                        node->descriptor->main_entry(CPIPE_ACTION_CREATE, host_context.host(),
                                                     nullptr, params.get(), nullptr, &instance));
                    if (node_status != CPIPE_OK) {
                        set_error(error, "node create failed: " + node->id);
                        return node_status;
                    }

                    auto input_handle = make_buffer_handle(current);
                    const auto input_count = node_input_count(node->descriptor);
                    std::vector<const cpipe_buffer_t*> process_inputs(input_count,
                                                                      input_handle.get());
                    std::shared_ptr<IBuffer> next;
                    std::unique_ptr<cpipe_buffer_t> output_handle;
                    std::unique_ptr<cpipe_metadata_builder_t> output_metadata_builder;
                    cpipe_buffer_t* process_outputs[] = {nullptr};
                    cpipe_metadata_builder_t* process_out_metadata[] = {nullptr};
                    std::size_t output_count = 0;
                    if (has_output) {
                        const auto output_layout = output_layout_for_node(
                            node->descriptor, node->params, current->layout(), error);
                        if (!output_layout) {
                            return CPIPE_FAILED;
                        }
                        next = std::make_shared<CpuBuffer>(
                            *output_layout,
                            BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
                        output_handle = make_buffer_handle(next);
                        std::vector<std::shared_ptr<const compute::BufferMetadata>> input_metadata(
                            input_count, current->metadata());
                        output_metadata_builder = make_metadata_builder_handle(
                            current->metadata(), std::move(input_metadata));
                        process_outputs[0] = output_handle.get();
                        process_out_metadata[0] = output_metadata_builder.get();
                        output_count = 1;
                    }

                    cpipe_process_ctx process{
                        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
                        .inference = nullptr,
                        .inputs = process_inputs.data(),
                        .n_in = process_inputs.size(),
                        .outputs = has_output ? process_outputs : nullptr,
                        .n_out = output_count,
                        .out_metadata = has_output ? process_out_metadata : nullptr,
                    };
                    node_status = static_cast<cpipe_status_t>(
                        node->descriptor->main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                                                     reinterpret_cast<cpipe_node_t*>(instance),
                                                     params.get(), &process, nullptr));
                    if (has_output) {
                        next->set_metadata(freeze_metadata_builder(output_metadata_builder.get()));
                    }

                    const auto destroy_status = static_cast<cpipe_status_t>(
                        node->descriptor->main_entry(CPIPE_ACTION_DESTROY, host_context.host(),
                                                     nullptr, nullptr, instance, nullptr));
                    if (node_status != CPIPE_OK) {
                        set_error(error, "node process failed: " + node->id);
                        return node_status;
                    }
                    if (destroy_status != CPIPE_OK) {
                        set_error(error, "node destroy failed: " + node->id);
                        return destroy_status;
                    }
                    if (has_output) {
                        current = std::move(next);
                    }
                    return CPIPE_OK;
                },
            .dependencies = std::move(dependencies),
        });
    }

    Scheduler scheduler;
    return scheduler.run(scheduled_nodes);
}

cpipe_status_t Pipeline::run_file(const std::filesystem::path& input_path,
                                  const std::filesystem::path& output_path,
                                  std::string* error) const {
    CPIPE_TRACE_SCOPE("Pipeline::run");

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
        const auto input_count = node_input_count(node.descriptor);
        std::vector<const cpipe_buffer_t*> process_inputs(input_count, input_handle.get());
        std::vector<std::shared_ptr<const compute::BufferMetadata>> input_metadata(
            input_count, current->metadata());
        auto output_metadata_builder =
            make_metadata_builder_handle(current->metadata(), std::move(input_metadata));
        cpipe_buffer_t* process_outputs[] = {output_handle.get()};
        cpipe_metadata_builder_t* process_out_metadata[] = {output_metadata_builder.get()};
        cpipe_process_ctx process{
            .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
            .inference = nullptr,
            .inputs = process_inputs.data(),
            .n_in = process_inputs.size(),
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
