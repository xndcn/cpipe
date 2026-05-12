// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/Pipeline.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json-schema.hpp>
#include <queue>
#include <unordered_map>
#include <utility>

#include "HostSuites.hpp"
#include "RuntimeHandles.hpp"
#include "cpipe/runtime/Scheduler.hpp"

namespace cpipe::runtime {

extern const char kPipelineSchemaJson[];

namespace {

constexpr std::string_view kGraphInput = "$input";
constexpr std::string_view kGraphOutput = "$output";

Result<nlohmann::json> read_json_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        return tl::unexpected(make_error(StatusCode::NotFound, "failed to open " + path.string()));
    }

    try {
        return nlohmann::json::parse(stream);
    } catch (const std::exception& err) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, err.what()));
    }
}

Result<void> validate_pipeline_schema(const nlohmann::json& document) {
    try {
        const auto schema = nlohmann::json::parse(kPipelineSchemaJson);
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(schema);
        validator.validate(document);
    } catch (const std::exception& err) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, err.what()));
    }
    return {};
}

StatusCode to_status_code(int status) noexcept {
    switch (status) {
        case CPIPE_OK:
            return StatusCode::Ok;
        case CPIPE_FAILED:
            return StatusCode::Failed;
        case CPIPE_REPLY_DEFAULT:
            return StatusCode::ReplyDefault;
        case CPIPE_OOM:
            return StatusCode::OutOfMemory;
        case CPIPE_BAD_PRECISION:
            return StatusCode::BadPrecision;
        case CPIPE_BAD_INDEX:
            return StatusCode::BadIndex;
        case CPIPE_NEED_PARAM:
            return StatusCode::NeedParam;
        case CPIPE_INTERNAL_ERROR:
            return StatusCode::InternalError;
        case CPIPE_UNSUPPORTED:
            return StatusCode::Unsupported;
        default:
            return StatusCode::Failed;
    }
}

}  // namespace

Pipeline::Pipeline(Pipeline&& other) noexcept
    : input_layout_(other.input_layout_),
      nodes_(std::exchange(other.nodes_, {})),
      edges_(std::exchange(other.edges_, {})),
      topo_order_(std::exchange(other.topo_order_, {})) {}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
    if (this != &other) {
        destroy_nodes();
        input_layout_ = other.input_layout_;
        nodes_ = std::exchange(other.nodes_, {});
        edges_ = std::exchange(other.edges_, {});
        topo_order_ = std::exchange(other.topo_order_, {});
    }
    return *this;
}

Pipeline::~Pipeline() {
    destroy_nodes();
}

Result<Pipeline> Pipeline::load(const std::filesystem::path& path) {
    auto document = read_json_file(path);
    if (!document) {
        return tl::unexpected(document.error());
    }
    auto schema_status = validate_pipeline_schema(*document);
    if (!schema_status) {
        return tl::unexpected(schema_status.error());
    }

    Registry registry;
    Pipeline pipeline;
    auto layout = parse_layout(document->at("input_layout"));
    if (!layout) {
        return tl::unexpected(layout.error());
    }
    pipeline.input_layout_ = *layout;

    for (const auto& node_json : document->at("nodes")) {
        PipelineNode node;
        node.id = node_json.at("id").get<std::string>();
        node.type = node_json.at("type").get<std::string>();
        node.params = node_json.value("params", nlohmann::json::object());
        node.descriptor = registry.find(node.type);
        if (node.descriptor == nullptr) {
            return tl::unexpected(
                make_error(StatusCode::NotFound, "unknown node type: " + node.type));
        }
        node.handle = new cpipe_node_t{};
        cpipe_props_t props{node.params};
        void* state = nullptr;
        auto host = make_host();
        const int status = node.descriptor->main_entry(CPIPE_ACTION_CREATE, &host, node.handle,
                                                       &props, &props, &state);
        if (status != CPIPE_OK && status != CPIPE_REPLY_DEFAULT) {
            delete node.handle;
            return tl::unexpected(
                make_error(to_status_code(status), "node create failed: " + node.id));
        }
        node.handle->instance_state = state;
        pipeline.nodes_.push_back(std::move(node));
    }

    for (const auto& edge_json : document->at("edges")) {
        pipeline.edges_.push_back(PipelineEdge{edge_json.at("from").get<std::string>(),
                                               edge_json.at("to").get<std::string>()});
    }

    auto order = sort_nodes(pipeline.nodes_, pipeline.edges_);
    if (!order) {
        return tl::unexpected(order.error());
    }
    pipeline.topo_order_ = *order;
    return pipeline;
}

Result<std::shared_ptr<compute::CpuBuffer>> Pipeline::run(
    const std::shared_ptr<compute::CpuBuffer>& input) {
    if (input == nullptr || input->size_bytes() != input_layout_.size_bytes()) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, "input layout mismatch"));
    }

    auto output = compute::CpuBuffer::create(
        input_layout_, compute::BufferUsage::CpuRead | compute::BufferUsage::CpuWrite);
    if (!output) {
        return tl::unexpected(output.error());
    }

    ComputeContext compute;
    InferenceContext inference;
    cpipe_compute_t compute_handle{&compute};
    cpipe_inference_t inference_handle{&inference};
    cpipe_buffer_t input_handle{input};
    cpipe_buffer_t output_handle{*output};
    auto host = make_host();
    Scheduler scheduler;

    std::unordered_map<std::string, std::shared_ptr<compute::IBuffer>> node_outputs;
    std::vector<ScheduleStep> steps;
    steps.reserve(topo_order_.size());

    for (const auto& node_id : topo_order_) {
        auto node_it = std::find_if(nodes_.begin(), nodes_.end(),
                                    [&node_id](const auto& node) { return node.id == node_id; });
        steps.push_back(ScheduleStep{
            node_id, [&, node_it]() -> StatusCode {
                const auto input_edge =
                    std::find_if(edges_.begin(), edges_.end(),
                                 [&](const auto& edge) { return edge.to == node_it->id; });
                const auto output_edge =
                    std::find_if(edges_.begin(), edges_.end(),
                                 [&](const auto& edge) { return edge.from == node_it->id; });
                if (input_edge == edges_.end() || output_edge == edges_.end()) {
                    return StatusCode::InvalidArgument;
                }

                cpipe_buffer_t* input_buffer = nullptr;
                cpipe_buffer_t intermediate_input;
                if (input_edge->from == kGraphInput) {
                    input_buffer = &input_handle;
                } else {
                    intermediate_input.buffer = node_outputs.at(input_edge->from);
                    input_buffer = &intermediate_input;
                }

                cpipe_buffer_t* output_buffer = nullptr;
                cpipe_buffer_t intermediate_output;
                if (output_edge->to == kGraphOutput) {
                    output_buffer = &output_handle;
                } else {
                    auto intermediate = compute::CpuBuffer::create(
                        input_layout_,
                        compute::BufferUsage::CpuRead | compute::BufferUsage::CpuWrite);
                    if (!intermediate) {
                        return StatusCode::OutOfMemory;
                    }
                    node_outputs[node_it->id] = *intermediate;
                    intermediate_output.buffer = *intermediate;
                    output_buffer = &intermediate_output;
                }

                const cpipe_buffer_t* inputs[] = {input_buffer};
                cpipe_buffer_t* outputs[] = {output_buffer};
                cpipe_process_ctx process_ctx{
                    &compute_handle, &inference_handle, inputs, 1, outputs, 1};
                cpipe_props_t props{node_it->params};
                const int status = node_it->descriptor->main_entry(
                    CPIPE_ACTION_PROCESS, &host, node_it->handle, &props, &process_ctx, nullptr);
                return to_status_code(status);
            }});
    }

    auto run_status = scheduler.run(steps);
    if (!run_status) {
        return tl::unexpected(run_status.error());
    }
    return *output;
}

Result<void> Pipeline::run_file(const std::filesystem::path& input_path,
                                const std::filesystem::path& output_path) {
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream) {
        return tl::unexpected(make_error(StatusCode::NotFound, "failed to open input"));
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(input_stream)),
                            std::istreambuf_iterator<char>());
    if (bytes.size() != input_layout_.size_bytes()) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, "input size mismatch"));
    }

    auto input = compute::CpuBuffer::create(
        input_layout_, compute::BufferUsage::CpuRead | compute::BufferUsage::CpuWrite);
    if (!input) {
        return tl::unexpected(input.error());
    }
    auto* input_ptr = (*input)->lock_cpu(compute::IBuffer::CpuAccess::Write);
    std::memcpy(input_ptr, bytes.data(), bytes.size());
    (*input)->unlock_cpu();
    (*input)->flush_cpu_writes();

    auto output = run(*input);
    if (!output) {
        return tl::unexpected(output.error());
    }

    std::ofstream output_stream(output_path, std::ios::binary);
    if (!output_stream) {
        return tl::unexpected(make_error(StatusCode::Failed, "failed to open output"));
    }
    const auto* output_bytes = static_cast<const char*>((*output)->data());
    output_stream.write(output_bytes, static_cast<std::streamsize>((*output)->size_bytes()));
    return {};
}

const std::vector<std::string>& Pipeline::topo_order() const noexcept {
    return topo_order_;
}

const compute::BufferLayout& Pipeline::input_layout() const noexcept {
    return input_layout_;
}

Result<compute::BufferLayout> Pipeline::parse_layout(const nlohmann::json& json) {
    const auto kind = json.at("kind").get<std::string>();
    const auto format = json.at("format").get<std::string>();
    const auto dims = json.at("dims").get<std::vector<uint32_t>>();
    if (kind != "Image2D" || format != "R8G8B8A8_UNORM" || dims.size() != 2) {
        return tl::unexpected(
            make_error(StatusCode::Unsupported, "P0 supports only R8G8B8A8 Image2D"));
    }
    return compute::make_rgba8_layout(dims[0], dims[1]);
}

Result<std::vector<std::string>> Pipeline::sort_nodes(const std::vector<PipelineNode>& nodes,
                                                      const std::vector<PipelineEdge>& edges) {
    std::unordered_map<std::string, std::size_t> indegree;
    std::unordered_map<std::string, std::vector<std::string>> outgoing;
    for (const auto& node : nodes) {
        indegree.emplace(node.id, 0);
    }

    for (const auto& edge : edges) {
        const bool from_graph = edge.from == kGraphInput || edge.from == kGraphOutput;
        const bool to_graph = edge.to == kGraphInput || edge.to == kGraphOutput;
        if (!from_graph && !indegree.contains(edge.from)) {
            return tl::unexpected(make_error(StatusCode::InvalidArgument, "dangling edge source"));
        }
        if (!to_graph && !indegree.contains(edge.to)) {
            return tl::unexpected(make_error(StatusCode::InvalidArgument, "dangling edge target"));
        }
        if (!from_graph && !to_graph) {
            outgoing[edge.from].push_back(edge.to);
            ++indegree[edge.to];
        }
    }

    std::queue<std::string> ready;
    for (const auto& [id, count] : indegree) {
        if (count == 0) {
            ready.push(id);
        }
    }

    std::vector<std::string> order;
    while (!ready.empty()) {
        auto id = ready.front();
        ready.pop();
        order.push_back(id);
        for (const auto& next : outgoing[id]) {
            --indegree[next];
            if (indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (order.size() != nodes.size()) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, "pipeline contains a cycle"));
    }
    return order;
}

void Pipeline::destroy_nodes() noexcept {
    auto host = make_host();
    for (auto& node : nodes_) {
        if (node.descriptor != nullptr && node.handle != nullptr) {
            static_cast<void>(node.descriptor->main_entry(CPIPE_ACTION_DESTROY, &host, node.handle,
                                                          nullptr, node.handle->instance_state,
                                                          nullptr));
        }
        delete node.handle;
        node.handle = nullptr;
    }
    nodes_.clear();
}

}  // namespace cpipe::runtime
