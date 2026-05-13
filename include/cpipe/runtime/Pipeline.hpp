// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace cpipe::runtime {

class Pipeline {
public:
    static cpipe_status_t load(const std::filesystem::path& path, const Registry& registry,
                               Pipeline* out, std::string* error);

    [[nodiscard]] cpipe_status_t run_file(const std::filesystem::path& input,
                                          const std::filesystem::path& output,
                                          std::string* error) const;

    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] const compute::BufferLayout& layout() const noexcept;

private:
    struct NodeInstance {
        std::string id;
        const cpipe_plugin_desc_t* descriptor{nullptr};
    };

    compute::BufferLayout layout_{};
    std::vector<NodeInstance> nodes_;
};

}  // namespace cpipe::runtime
