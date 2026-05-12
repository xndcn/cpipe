// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/Registry.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

extern "C" {
extern const cpipe_plugin_desc_t __start_cpipe_registry[] __attribute__((weak));
extern const cpipe_plugin_desc_t __stop_cpipe_registry[] __attribute__((weak));
}

namespace cpipe::runtime {

Registry::Registry() : descriptors_(load_builtin_nodes()) {}

const cpipe_plugin_desc_t* Registry::find(std::string_view node_id) const noexcept {
    const auto it =
        std::find_if(descriptors_.begin(), descriptors_.end(), [node_id](const auto* desc) {
            return desc != nullptr && desc->node_id != nullptr && node_id == desc->node_id;
        });
    return it == descriptors_.end() ? nullptr : *it;
}

const std::vector<const cpipe_plugin_desc_t*>& Registry::descriptors() const noexcept {
    return descriptors_;
}

std::vector<const cpipe_plugin_desc_t*> Registry::load_builtin_nodes() {
    std::vector<const cpipe_plugin_desc_t*> descriptors;
    if (__start_cpipe_registry == nullptr || __stop_cpipe_registry == nullptr) {
        return descriptors;
    }

    for (const cpipe_plugin_desc_t* desc = __start_cpipe_registry; desc != __stop_cpipe_registry;
         ++desc) {
        if (desc->abi_major != CPIPE_ABI_MAJOR || desc->abi_minor > CPIPE_ABI_MINOR) {
            spdlog::warn("Skipping cpipe node with unsupported ABI {}.{}", desc->abi_major,
                         desc->abi_minor);
            continue;
        }
        descriptors.push_back(desc);
    }
    return descriptors;
}

}  // namespace cpipe::runtime
