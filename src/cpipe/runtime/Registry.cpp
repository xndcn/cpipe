// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/Registry.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "RuntimeSuites.hpp"
#include "cpipe/sdk/section.hpp"

namespace cpipe::runtime {
namespace {

auto missing_param_double(const cpipe_props_t* props, const char* key, double* out) -> int {
    (void)props;
    (void)key;
    if (out != nullptr) {
        *out = 0.0;
    }
    return CPIPE_NEED_PARAM;
}

auto missing_param_int(const cpipe_props_t* props, const char* key, std::int64_t* out) -> int {
    (void)props;
    (void)key;
    if (out != nullptr) {
        *out = 0;
    }
    return CPIPE_NEED_PARAM;
}

auto missing_param_bool(const cpipe_props_t* props, const char* key, int* out) -> int {
    (void)props;
    (void)key;
    if (out != nullptr) {
        *out = 0;
    }
    return CPIPE_NEED_PARAM;
}

auto missing_param_enum(const cpipe_props_t* props, const char* key, const char** out) -> int {
    (void)props;
    (void)key;
    if (out != nullptr) {
        *out = nullptr;
    }
    return CPIPE_NEED_PARAM;
}

auto missing_param_curve(const cpipe_props_t* props, const char* key, const float** xs,
                         const float** ys, std::size_t* n) -> int {
    (void)props;
    (void)key;
    if (xs != nullptr) {
        *xs = nullptr;
    }
    if (ys != nullptr) {
        *ys = nullptr;
    }
    if (n != nullptr) {
        *n = 0;
    }
    return CPIPE_NEED_PARAM;
}

auto missing_param_color(const cpipe_props_t* props, const char* key, float* rgba) -> int {
    (void)props;
    (void)key;
    if (rgba != nullptr) {
        std::fill_n(rgba, 4, 0.0F);
    }
    return CPIPE_NEED_PARAM;
}

constexpr cpipe_param_suite_v1 kParamSuite{&missing_param_double, &missing_param_int,
                                           &missing_param_bool,   &missing_param_enum,
                                           &missing_param_curve,  &missing_param_color};

auto get_suite(cpipe_host_t* self, const char* suite_name, int version) -> const void* {
    (void)self;
    if (suite_name == nullptr || version != 1) {
        return nullptr;
    }
    if (std::strcmp(suite_name, "buffer") == 0) {
        return detail::buffer_suite();
    }
    if (std::strcmp(suite_name, "compute") == 0) {
        return detail::compute_suite();
    }
    if (std::strcmp(suite_name, "param") == 0) {
        return &kParamSuite;
    }
    if (std::strcmp(suite_name, "inference") == 0) {
        return detail::inference_suite();
    }
    return nullptr;
}

void log_noop(cpipe_host_t* self, int level, const char* msg) {
    (void)self;
    (void)level;
    (void)msg;
}

auto host_alloc(cpipe_host_t* self, std::size_t bytes) -> void* {
    (void)self;
    return std::malloc(bytes);
}

void host_free(cpipe_host_t* self, void* ptr) {
    (void)self;
    std::free(ptr);
}

}  // namespace

auto Registry::load_builtin_nodes() -> std::size_t {
    descriptors_.clear();

    const auto* begin = sdk::detail::registry_section_begin();
    const auto* end = sdk::detail::registry_section_end();
    if (begin == nullptr || end == nullptr) {
        return 0U;
    }

    for (const auto* desc = begin; desc != end; ++desc) {
        if (desc->abi_major != CPIPE_ABI_MAJOR || desc->abi_minor > CPIPE_ABI_MINOR ||
            desc->node_id == nullptr || desc->main_entry == nullptr) {
            continue;
        }
        descriptors_.push_back(desc);
    }
    return descriptors_.size();
}

auto Registry::find(std::string_view node_id) const noexcept -> const cpipe_plugin_desc_t* {
    for (const auto* desc : descriptors_) {
        if (desc != nullptr && desc->node_id != nullptr && desc->node_id == node_id) {
            return desc;
        }
    }
    return nullptr;
}

auto Registry::descriptors() const noexcept -> std::span<const cpipe_plugin_desc_t* const> {
    return descriptors_;
}

auto make_host() noexcept -> cpipe_host_t {
    return cpipe_host_t{CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, &get_suite,
                        &log_noop,       &host_alloc,     &host_free};
}

}  // namespace cpipe::runtime
