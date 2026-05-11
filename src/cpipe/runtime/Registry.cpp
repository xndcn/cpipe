// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <cpipe/runtime/Registry.hpp>

extern "C" {
extern const cpipe_plugin_desc_t __start_cpipe_registry[] __attribute__((weak));
extern const cpipe_plugin_desc_t __stop_cpipe_registry[] __attribute__((weak));
}

namespace cpipe::runtime {

Registry Registry::load_builtin_nodes() {
    Registry registry;
    if (__start_cpipe_registry == nullptr || __stop_cpipe_registry == nullptr) {
        return registry;
    }

    for (auto* descriptor = __start_cpipe_registry; descriptor < __stop_cpipe_registry;
         ++descriptor) {
        registry.add(descriptor);
    }
    return registry;
}

void Registry::add(const cpipe_plugin_desc_t* descriptor) {
    if (descriptor == nullptr) {
        return;
    }
    if (descriptor->abi_major != CPIPE_ABI_MAJOR || descriptor->abi_minor > CPIPE_ABI_MINOR) {
        spdlog::warn("skipping {} with unsupported ABI v{}.{}",
                     descriptor->node_id == nullptr ? "<unknown>" : descriptor->node_id,
                     descriptor->abi_major, descriptor->abi_minor);
        return;
    }
    descriptors_.push_back(descriptor);
}

const cpipe_plugin_desc_t* Registry::find(std::string_view node_id) const noexcept {
    for (const auto* descriptor : descriptors_) {
        if (descriptor != nullptr && descriptor->node_id != nullptr &&
            node_id == descriptor->node_id) {
            return descriptor;
        }
    }
    return nullptr;
}

std::size_t Registry::size() const noexcept {
    return descriptors_.size();
}

}  // namespace cpipe::runtime
