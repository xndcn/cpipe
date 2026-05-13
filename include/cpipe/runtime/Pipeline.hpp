// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cpipe::runtime {

class Pipeline {
public:
    static cpipe_status_t load(const std::filesystem::path& path, const Registry& registry,
                               Pipeline* out, std::string* error);

    [[nodiscard]] cpipe_status_t set_source(std::string port_name, std::string plugin_id,
                                            nlohmann::json params);
    [[nodiscard]] cpipe_status_t run(std::string* error) const;
    [[nodiscard]] cpipe_status_t run_to_file(const std::filesystem::path& output,
                                             std::string* error) const;
    [[nodiscard]] cpipe_status_t run_file(const std::filesystem::path& input,
                                          const std::filesystem::path& output,
                                          std::string* error) const;

    void set_device_memory_cap(std::uint64_t bytes) noexcept;

    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] const compute::BufferLayout& layout() const noexcept;
    [[nodiscard]] std::uint64_t memory_peak_bytes() const noexcept;

private:
    struct InputPort {
        std::string name;
        compute::BufferLayout layout{};
    };

    struct NodeInstance {
        std::string id;
        const cpipe_plugin_desc_t* descriptor{nullptr};
        nlohmann::json params;
    };

    struct SourceBinding {
        std::string plugin_id;
        const cpipe_plugin_desc_t* descriptor{nullptr};
        nlohmann::json params;
    };

    [[nodiscard]] cpipe_status_t run_bound(std::optional<std::filesystem::path> output,
                                           std::string* error) const;

    std::vector<InputPort> inputs_;
    compute::BufferLayout layout_{};
    std::vector<NodeInstance> nodes_;
    std::unordered_map<std::string, SourceBinding> sources_;
    const Registry* registry_{nullptr};
    std::uint64_t device_memory_cap_bytes_{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t memory_peak_bytes_{0};
};

}  // namespace cpipe::runtime
