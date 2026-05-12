// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class Registry {
public:
    auto load_builtin_nodes() -> std::size_t;

    [[nodiscard]] auto find(std::string_view node_id) const noexcept -> const cpipe_plugin_desc_t*;

    [[nodiscard]] auto descriptors() const noexcept -> std::span<const cpipe_plugin_desc_t* const>;

private:
    std::vector<const cpipe_plugin_desc_t*> descriptors_;
};

[[nodiscard]] auto make_host() noexcept -> cpipe_host_t;

}  // namespace cpipe::runtime
