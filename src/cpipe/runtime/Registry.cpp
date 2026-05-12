// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/section.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-reserved-identifier)
extern const cpipe_plugin_desc_t __start_cpipe_registry[] __attribute__((weak));
extern const cpipe_plugin_desc_t __stop_cpipe_registry[] __attribute__((weak));
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-reserved-identifier)
}

namespace cpipe::runtime {
namespace {

// NOLINTBEGIN(readability-named-parameter,bugprone-easily-swappable-parameters)

auto unsupported_get_dims(const cpipe_buffer_t*, std::uint8_t*, std::uint32_t*) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_get_int_metadata(const cpipe_buffer_t*, int*) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_get_stride(const cpipe_buffer_t*, std::uint64_t*) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_get_color_role(const cpipe_buffer_t*, const char**) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_lock_cpu(cpipe_buffer_t*, int, void**) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_buffer_mutation(cpipe_buffer_t*) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_submit_halide(cpipe_compute_t*, const char*, const cpipe_buffer_t* const*,
                               std::size_t, cpipe_buffer_t* const*, std::size_t) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_submit_slang(cpipe_compute_t*, const char*, const char*,
                              const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                              std::size_t, const void*, std::size_t) -> int {
    return CPIPE_UNSUPPORTED;
}

auto unsupported_request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**) -> int {
    return CPIPE_UNSUPPORTED;
}

auto ignore_marker(cpipe_compute_t*, const char*) -> void {}

auto unsupported_submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                  std::size_t, cpipe_buffer_t* const*, std::size_t) -> int {
    return CPIPE_UNSUPPORTED;
}

auto missing_double(const cpipe_props_t*, const char*, double*) -> int {
    return CPIPE_NEED_PARAM;
}

auto missing_int(const cpipe_props_t*, const char*, std::int64_t*) -> int {
    return CPIPE_NEED_PARAM;
}

auto missing_bool(const cpipe_props_t*, const char*, int*) -> int {
    return CPIPE_NEED_PARAM;
}

auto missing_enum(const cpipe_props_t*, const char*, const char**) -> int {
    return CPIPE_NEED_PARAM;
}

auto missing_curve(const cpipe_props_t*, const char*, const float**, const float**, std::size_t*)
    -> int {
    return CPIPE_NEED_PARAM;
}

auto missing_color(const cpipe_props_t*, const char*, float*) -> int {
    return CPIPE_NEED_PARAM;
}

const cpipe_buffer_suite_v1 kBufferSuite{
    &unsupported_get_dims,        &unsupported_get_int_metadata, &unsupported_get_int_metadata,
    &unsupported_get_stride,      &unsupported_get_color_role,   &unsupported_lock_cpu,
    &unsupported_buffer_mutation, &unsupported_buffer_mutation};

const cpipe_compute_suite_v1 kComputeSuite{&unsupported_submit_halide, &unsupported_submit_slang,
                                           &unsupported_request_scratch, &ignore_marker};

const cpipe_inference_suite_v1 kInferenceSuite{&unsupported_submit_inference};

const cpipe_param_suite_v1 kParamSuite{&missing_double, &missing_int,   &missing_bool,
                                       &missing_enum,   &missing_curve, &missing_color};

auto get_suite(cpipe_host_t*, const char* suite_name, int version) -> const void* {
    if (suite_name == nullptr || version != 1) {
        return nullptr;
    }
    if (std::strcmp(suite_name, "buffer") == 0) {
        return &kBufferSuite;
    }
    if (std::strcmp(suite_name, "compute") == 0) {
        return &kComputeSuite;
    }
    if (std::strcmp(suite_name, "param") == 0) {
        return &kParamSuite;
    }
    if (std::strcmp(suite_name, "inference") == 0) {
        return &kInferenceSuite;
    }
    return nullptr;
}

auto log_message(cpipe_host_t*, int, const char* msg) -> void {
    if (msg != nullptr) {
        std::clog << msg << '\n';
    }
}

auto allocate(cpipe_host_t*, std::size_t bytes) -> void* {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return std::malloc(bytes);
}

auto deallocate(cpipe_host_t*, void* ptr) -> void {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(ptr);
}

// NOLINTEND(readability-named-parameter,bugprone-easily-swappable-parameters)

}  // namespace

auto Registry::load_builtin_nodes() -> void {
    descriptors_.clear();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    const auto* begin = __start_cpipe_registry;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    const auto* end = __stop_cpipe_registry;
    if (begin == nullptr || end == nullptr) {
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (const auto* descriptor = begin; descriptor != end; ++descriptor) {
        register_descriptor(*descriptor);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

auto Registry::register_descriptor(const cpipe_plugin_desc_t& descriptor) -> void {
    if (descriptor.node_id == nullptr || descriptor.main_entry == nullptr) {
        return;
    }
    if (descriptor.abi_major != CPIPE_ABI_MAJOR || descriptor.abi_minor > CPIPE_ABI_MINOR) {
        return;
    }
    descriptors_.push_back(&descriptor);
}

auto Registry::find(std::string_view node_id) const noexcept -> const cpipe_plugin_desc_t* {
    for (const auto* descriptor : descriptors_) {
        if (descriptor != nullptr && descriptor->node_id != nullptr &&
            node_id == descriptor->node_id) {
            return descriptor;
        }
    }
    return nullptr;
}

auto Registry::size() const noexcept -> std::size_t {
    return descriptors_.size();
}

auto make_default_host() noexcept -> cpipe_host_t {
    return cpipe_host_t{CPIPE_ABI_MAJOR, CPIPE_ABI_MINOR, &get_suite,
                        &log_message,    &allocate,       &deallocate};
}

auto inference_suite_v1() noexcept -> const cpipe_inference_suite_v1& {
    return kInferenceSuite;
}

}  // namespace cpipe::runtime
