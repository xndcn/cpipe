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
    static Registry load_builtin_nodes();

    void add(const cpipe_plugin_desc_t* descriptor);

    [[nodiscard]] const cpipe_plugin_desc_t* find(std::string_view node_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<const cpipe_plugin_desc_t*> descriptors_;
};

}  // namespace cpipe::runtime
