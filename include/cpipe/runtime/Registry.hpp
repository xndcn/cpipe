// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <string_view>
#include <vector>

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class Registry {
public:
    Registry();

    [[nodiscard]] const cpipe_plugin_desc_t* find(std::string_view node_id) const noexcept;
    [[nodiscard]] const std::vector<const cpipe_plugin_desc_t*>& descriptors() const noexcept;

    static std::vector<const cpipe_plugin_desc_t*> load_builtin_nodes();

private:
    std::vector<const cpipe_plugin_desc_t*> descriptors_;
};

}  // namespace cpipe::runtime
