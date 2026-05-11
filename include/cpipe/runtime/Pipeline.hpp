// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/runtime/ComputeContext.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cpipe::runtime {

class Pipeline {
public:
    static std::optional<Pipeline> load_file(const std::filesystem::path& path, std::string* error);

    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] cpipe_status_t run_file(const std::filesystem::path& input_path,
                                          const std::filesystem::path& output_path,
                                          ComputeContext& compute_context,
                                          std::string* error) const;

private:
    struct NodeInstance {
        std::string id;
        const cpipe_plugin_desc_t* descriptor = nullptr;
    };

    std::vector<NodeInstance> nodes_;
};

}  // namespace cpipe::runtime
