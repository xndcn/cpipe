// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cpipe/core/CpuBuffer.hpp"
#include "cpipe/core/Status.hpp"
#include "cpipe/runtime/Registry.hpp"

namespace cpipe::runtime {

struct PipelineEdge {
    std::string from;
    std::string to;
};

struct PipelineNode {
    std::string id;
    std::string type;
    nlohmann::json params;
    const cpipe_plugin_desc_t* descriptor = nullptr;
    cpipe_node_t* handle = nullptr;
};

class Pipeline {
public:
    Pipeline() = default;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;
    ~Pipeline();

    static Result<Pipeline> load(const std::filesystem::path& path);

    [[nodiscard]] Result<std::shared_ptr<compute::CpuBuffer>> run(
        const std::shared_ptr<compute::CpuBuffer>& input);
    [[nodiscard]] Result<void> run_file(const std::filesystem::path& input_path,
                                        const std::filesystem::path& output_path);

    [[nodiscard]] const std::vector<std::string>& topo_order() const noexcept;
    [[nodiscard]] const compute::BufferLayout& input_layout() const noexcept;

private:
    static Result<compute::BufferLayout> parse_layout(const nlohmann::json& json);
    static Result<std::vector<std::string>> sort_nodes(const std::vector<PipelineNode>& nodes,
                                                       const std::vector<PipelineEdge>& edges);
    void destroy_nodes() noexcept;

    compute::BufferLayout input_layout_;
    std::vector<PipelineNode> nodes_;
    std::vector<PipelineEdge> edges_;
    std::vector<std::string> topo_order_;
};

}  // namespace cpipe::runtime
