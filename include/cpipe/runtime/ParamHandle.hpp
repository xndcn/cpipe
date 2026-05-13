// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <memory>
#include <nlohmann/json.hpp>

struct cpipe_props_s {
    nlohmann::json params;
};

namespace cpipe::runtime {

[[nodiscard]] std::unique_ptr<cpipe_props_t> make_param_handle(nlohmann::json params);

[[nodiscard]] const nlohmann::json* params_from_handle(const cpipe_props_t* handle);

}  // namespace cpipe::runtime
