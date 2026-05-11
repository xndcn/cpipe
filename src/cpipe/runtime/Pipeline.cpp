// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Pipeline.hpp>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/AbiBridge.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/InferenceContext.hpp>
#include <cpipe/runtime/Registry.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <utility>

extern const char PIPELINE_SCHEMA_JSON[];

namespace cpipe::runtime {
namespace {

using Json = nlohmann::json;

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

[[nodiscard]] bool is_external_endpoint(const std::string& id) {
    return id == "$input" || id == "$output";
}

[[nodiscard]] std::optional<Json> read_json_file(const std::filesystem::path& path,
                                                 std::string* error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        set_error(error, "failed to open pipeline JSON: " + path.string());
        return std::nullopt;
    }

    try {
        return std::optional<Json>{Json::parse(file)};
    } catch (const std::exception& ex) {
        set_error(error, std::string("failed to parse pipeline JSON: ") + ex.what());
        return std::nullopt;
    }
}

[[nodiscard]] bool validate_schema(const Json& document, std::string* error) {
    try {
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(Json::parse(PIPELINE_SCHEMA_JSON));
        validator.validate(document);
        return true;
    } catch (const std::exception& ex) {
        set_error(error, std::string("pipeline schema validation failed: ") + ex.what());
        return false;
    }
}

[[nodiscard]] std::optional<std::vector<std::string>> topo_order(const Json& document,
                                                                 std::string* error) {
    std::unordered_map<std::string, std::size_t> index;
    const auto& nodes = document.at("nodes");
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto id = nodes[i].at("id").get<std::string>();
        if (!index.emplace(id, i).second) {
            set_error(error, "duplicate node id: " + id);
            return std::nullopt;
        }
    }

    std::vector<std::vector<std::size_t>> outgoing(nodes.size());
    std::vector<std::size_t> indegree(nodes.size(), 0);
    for (const auto& edge : document.at("edges")) {
        const auto from = edge.at("from").get<std::string>();
        const auto to = edge.at("to").get<std::string>();
        const auto from_node = index.find(from);
        const auto to_node = index.find(to);

        if (from_node == index.end() && !is_external_endpoint(from)) {
            set_error(error, "dangling edge from: " + from);
            return std::nullopt;
        }
        if (to_node == index.end() && !is_external_endpoint(to)) {
            set_error(error, "dangling edge to: " + to);
            return std::nullopt;
        }
        if (from_node != index.end() && to_node != index.end()) {
            outgoing[from_node->second].push_back(to_node->second);
            ++indegree[to_node->second];
        }
    }

    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<std::string> order;
    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop();
        order.push_back(nodes[current].at("id").get<std::string>());
        for (const auto next : outgoing[current]) {
            --indegree[next];
            if (indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (order.size() != nodes.size()) {
        set_error(error, "pipeline graph contains a cycle");
        return std::nullopt;
    }
    return order;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_binary(
    const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        set_error(error, "failed to open input: " + path.string());
        return std::nullopt;
    }
    const auto bytes = std::filesystem::file_size(path);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    return data;
}

[[nodiscard]] bool write_binary(const std::filesystem::path& path,
                                const std::vector<std::uint8_t>& data, std::string* error) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        set_error(error, "failed to open output: " + path.string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return file.good();
}

}  // namespace

std::optional<Pipeline> Pipeline::load_file(const std::filesystem::path& path, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }

    const auto document = read_json_file(path, error);
    if (!document.has_value() || !validate_schema(*document, error)) {
        return std::nullopt;
    }

    const auto registry = Registry::load_builtin_nodes();
    const auto order = topo_order(*document, error);
    if (!order.has_value()) {
        return std::nullopt;
    }

    std::unordered_map<std::string, const cpipe_plugin_desc_t*> descriptors;
    for (const auto& node : document->at("nodes")) {
        const auto id = node.at("id").get<std::string>();
        const auto type = node.at("type").get<std::string>();
        const auto* desc = registry.find(type);
        if (desc == nullptr) {
            set_error(error, "unknown node type: " + type);
            return std::nullopt;
        }
        descriptors.emplace(id, desc);
    }

    Pipeline pipeline;
    pipeline.nodes_.reserve(order->size());
    for (const auto& id : *order) {
        pipeline.nodes_.push_back({id, descriptors.at(id)});
    }
    return pipeline;
}

std::size_t Pipeline::node_count() const noexcept {
    return nodes_.size();
}

cpipe_status_t Pipeline::run_file(const std::filesystem::path& input_path,
                                  const std::filesystem::path& output_path,
                                  ComputeContext& compute_context, std::string* error) const {
    const auto input_bytes = read_binary(input_path, error);
    if (!input_bytes.has_value()) {
        return CPIPE_FAILED;
    }

    cpipe::compute::BufferLayout layout{
        .kind = cpipe::compute::BufferKind::Blob,
        .format = cpipe::compute::PixelFormat::BLOB,
        .ndim = 1,
        .dims = {static_cast<std::uint32_t>(input_bytes->size())},
        .stride = {},
    };
    cpipe::compute::CpuBuffer input(
        layout, cpipe::compute::BufferUsage::Input | cpipe::compute::BufferUsage::CpuRead |
                    cpipe::compute::BufferUsage::CpuWrite);
    cpipe::compute::CpuBuffer output(
        layout, cpipe::compute::BufferUsage::Output | cpipe::compute::BufferUsage::CpuRead |
                    cpipe::compute::BufferUsage::CpuWrite);

    auto* dst =
        static_cast<std::uint8_t*>(input.lock_cpu(cpipe::compute::IBuffer::CpuAccess::ReadWrite));
    if (dst == nullptr) {
        set_error(error, "failed to lock input buffer");
        return CPIPE_FAILED;
    }
    std::memcpy(dst, input_bytes->data(), input_bytes->size());
    input.unlock_cpu();

    HostContext host_context;
    InferenceContext inference;
    ComputeHandle compute_handle(compute_context);
    InferenceHandle inference_handle(inference);
    BufferHandle input_handle(input);
    BufferHandle output_handle(output);

    const cpipe_buffer_t* inputs[] = {input_handle.c_buffer()};
    cpipe_buffer_t* outputs[] = {output_handle.c_buffer()};
    cpipe_process_ctx process_ctx{
        .compute = compute_handle.c_compute(),
        .inference = inference_handle.c_inference(),
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
    };

    for (const auto& node : nodes_) {
        void* state = nullptr;
        auto status = static_cast<cpipe_status_t>(node.descriptor->main_entry(
            CPIPE_ACTION_CREATE, host_context.c_host(), nullptr, nullptr, nullptr, &state));
        if (status == CPIPE_OK) {
            status = static_cast<cpipe_status_t>(node.descriptor->main_entry(
                CPIPE_ACTION_PROCESS, host_context.c_host(), reinterpret_cast<cpipe_node_t*>(state),
                nullptr, &process_ctx, nullptr));
        }
        static_cast<void>(node.descriptor->main_entry(CPIPE_ACTION_DESTROY, host_context.c_host(),
                                                      nullptr, nullptr, state, nullptr));
        if (status != CPIPE_OK) {
            set_error(error, "pipeline node failed: " + node.id);
            return status;
        }
    }

    const auto* src =
        static_cast<const std::uint8_t*>(output.lock_cpu(cpipe::compute::IBuffer::CpuAccess::Read));
    if (src == nullptr) {
        set_error(error, "failed to lock output buffer");
        return CPIPE_FAILED;
    }
    std::vector<std::uint8_t> output_bytes(src, src + input_bytes->size());
    output.unlock_cpu();

    return write_binary(output_path, output_bytes, error) ? CPIPE_OK : CPIPE_FAILED;
}

}  // namespace cpipe::runtime
