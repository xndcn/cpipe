// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/runtime/HalideFilterRegistry.hpp>

namespace cpipe::runtime {

HalideFilterRegistry& HalideFilterRegistry::instance() {
    static HalideFilterRegistry registry;
    return registry;
}

void HalideFilterRegistry::register_halide_filter(std::string_view aot_id,
                                                  HalideFilterEntry entry) {
    std::scoped_lock lock{mutex_};
    halide_filters_.insert_or_assign(std::string{aot_id}, entry);
}

void HalideFilterRegistry::register_halide_param_filter(std::string_view aot_id,
                                                        HalideParamFilterEntry entry) {
    std::scoped_lock lock{mutex_};
    halide_param_filters_.insert_or_assign(std::string{aot_id}, entry);
}

std::unordered_map<std::string, HalideFilterEntry> HalideFilterRegistry::halide_filters() const {
    std::scoped_lock lock{mutex_};
    return halide_filters_;
}

std::unordered_map<std::string, HalideParamFilterEntry> HalideFilterRegistry::halide_param_filters()
    const {
    std::scoped_lock lock{mutex_};
    return halide_param_filters_;
}

std::vector<std::string> HalideFilterRegistry::halide_filter_ids() const {
    std::scoped_lock lock{mutex_};
    std::vector<std::string> ids;
    ids.reserve(halide_filters_.size());
    for (const auto& [id, entry] : halide_filters_) {
        if (entry != nullptr) {
            ids.push_back(id);
        }
    }
    std::ranges::sort(ids);
    return ids;
}

}  // namespace cpipe::runtime
