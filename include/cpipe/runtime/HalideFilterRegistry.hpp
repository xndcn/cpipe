// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>

#include <cpipe/sdk/registry.hpp>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cpipe::runtime {

using HalideFilterEntry = int (*)(halide_buffer_t* input, halide_buffer_t* output);
using HalideParamFilterEntry = int (*)(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                       halide_buffer_t* const* outputs, std::size_t n_outputs,
                                       const void* param_blob, std::size_t param_blob_size);

class HalideFilterRegistry {
public:
    static HalideFilterRegistry& instance();

    void register_halide_filter(std::string_view aot_id, HalideFilterEntry entry);
    void register_halide_param_filter(std::string_view aot_id, HalideParamFilterEntry entry);

    [[nodiscard]] std::unordered_map<std::string, HalideFilterEntry> halide_filters() const;
    [[nodiscard]] std::unordered_map<std::string, HalideParamFilterEntry> halide_param_filters()
        const;
    [[nodiscard]] std::vector<std::string> halide_filter_ids() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, HalideFilterEntry> halide_filters_;
    std::unordered_map<std::string, HalideParamFilterEntry> halide_param_filters_;
};

}  // namespace cpipe::runtime

#define CPIPE_REGISTER_HALIDE_FILTER(aot_id_literal, entry_ptr) \
    CPIPE_REGISTER_HALIDE_FILTER_IMPL(aot_id_literal, entry_ptr, __COUNTER__)

#define CPIPE_REGISTER_HALIDE_FILTER_IMPL(aot_id_literal, entry_ptr, counter)          \
    namespace {                                                                        \
    struct CPIPE_DETAIL_CONCAT(cpipe_halide_filter_registrar_, counter) {              \
        CPIPE_DETAIL_CONCAT(cpipe_halide_filter_registrar_, counter)() {               \
            ::cpipe::runtime::HalideFilterRegistry::instance().register_halide_filter( \
                aot_id_literal, entry_ptr);                                            \
        }                                                                              \
    };                                                                                 \
    const CPIPE_DETAIL_CONCAT(cpipe_halide_filter_registrar_, counter)                 \
        CPIPE_DETAIL_CONCAT(cpipe_halide_filter_registrar_instance_, counter){};       \
    }

#define CPIPE_REGISTER_HALIDE_PARAM_FILTER(aot_id_literal, entry_ptr) \
    CPIPE_REGISTER_HALIDE_PARAM_FILTER_IMPL(aot_id_literal, entry_ptr, __COUNTER__)

#define CPIPE_REGISTER_HALIDE_PARAM_FILTER_IMPL(aot_id_literal, entry_ptr, counter)          \
    namespace {                                                                              \
    struct CPIPE_DETAIL_CONCAT(cpipe_halide_param_filter_registrar_, counter) {              \
        CPIPE_DETAIL_CONCAT(cpipe_halide_param_filter_registrar_, counter)() {               \
            ::cpipe::runtime::HalideFilterRegistry::instance().register_halide_param_filter( \
                aot_id_literal, entry_ptr);                                                  \
        }                                                                                    \
    };                                                                                       \
    const CPIPE_DETAIL_CONCAT(cpipe_halide_param_filter_registrar_, counter)                 \
        CPIPE_DETAIL_CONCAT(cpipe_halide_param_filter_registrar_instance_, counter){};       \
    }
