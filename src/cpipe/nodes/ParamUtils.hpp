// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <algorithm>
#include <cpipe/sdk/sdk.hpp>
#include <string>
#include <string_view>

namespace cpipe::nodes {

inline double param_double_or(const sdk::ParamView& params, std::string_view key, double fallback) {
    if (params.suite() == nullptr || params.suite()->get_double == nullptr ||
        params.impl() == nullptr) {
        return fallback;
    }
    const std::string key_string{key};
    double value = 0.0;
    const auto status = static_cast<cpipe_status_t>(
        params.suite()->get_double(params.impl(), key_string.c_str(), &value));
    return status == CPIPE_OK ? value : fallback;
}

inline float clamped_param_float_or(const sdk::ParamView& params, std::string_view key,
                                    float fallback, float min_value, float max_value) {
    return std::clamp(static_cast<float>(param_double_or(params, key, fallback)), min_value,
                      max_value);
}

inline int clamped_param_int_or(const sdk::ParamView& params, std::string_view key, int fallback,
                                int min_value, int max_value) {
    return std::clamp(static_cast<int>(param_double_or(params, key, fallback)), min_value,
                      max_value);
}

inline bool param_bool_or(const sdk::ParamView& params, std::string_view key, bool fallback) {
    if (params.suite() == nullptr || params.suite()->get_bool == nullptr ||
        params.impl() == nullptr) {
        return fallback;
    }
    const std::string key_string{key};
    int value = 0;
    const auto status = static_cast<cpipe_status_t>(
        params.suite()->get_bool(params.impl(), key_string.c_str(), &value));
    return status == CPIPE_OK ? value != 0 : fallback;
}

inline std::string param_string_or(const sdk::ParamView& params, std::string_view key,
                                   std::string_view fallback) {
    if (params.suite() == nullptr || params.suite()->get_enum == nullptr ||
        params.impl() == nullptr) {
        return std::string{fallback};
    }
    const std::string key_string{key};
    const char* value = nullptr;
    const auto status = static_cast<cpipe_status_t>(
        params.suite()->get_enum(params.impl(), key_string.c_str(), &value));
    return status == CPIPE_OK && value != nullptr ? std::string{value} : std::string{fallback};
}

}  // namespace cpipe::nodes
