// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace cpipe::runtime {

class Registry {
public:
    auto load_builtin_nodes() -> void;

    [[nodiscard]] auto find(std::string_view node_id) const noexcept -> const cpipe_plugin_desc_t*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    auto register_descriptor(const cpipe_plugin_desc_t& descriptor) -> void;

    std::vector<const cpipe_plugin_desc_t*> descriptors_;
};

[[nodiscard]] auto make_default_host() noexcept -> cpipe_host_t;
[[nodiscard]] auto inference_suite_v1() noexcept -> const cpipe_inference_suite_v1&;

}  // namespace cpipe::runtime
