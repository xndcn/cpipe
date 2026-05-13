// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Registry.hpp>

extern "C" {
extern const cpipe_plugin_desc_t __start_cpipe_registry[] __attribute__((weak));
extern const cpipe_plugin_desc_t __stop_cpipe_registry[] __attribute__((weak));
}

namespace cpipe::runtime {

void Registry::load_builtin_nodes() {
    descriptors_.clear();
    if (__start_cpipe_registry == nullptr || __stop_cpipe_registry == nullptr) {
        return;
    }

    for (auto* desc = __start_cpipe_registry; desc < __stop_cpipe_registry; ++desc) {
        if (desc->node_id == nullptr || desc->main_entry == nullptr ||
            desc->abi_major != CPIPE_ABI_MAJOR || desc->abi_minor > CPIPE_ABI_MINOR) {
            continue;
        }
        descriptors_.push_back(desc);
    }
}

const cpipe_plugin_desc_t* Registry::find(std::string_view node_id) const noexcept {
    for (const auto* desc : descriptors_) {
        if (desc != nullptr && desc->node_id != nullptr && desc->node_id == node_id) {
            return desc;
        }
    }
    return nullptr;
}

const std::vector<const cpipe_plugin_desc_t*>& Registry::descriptors() const noexcept {
    return descriptors_;
}

}  // namespace cpipe::runtime
