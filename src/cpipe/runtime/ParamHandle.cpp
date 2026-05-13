// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/ParamHandle.hpp>
#include <utility>

namespace cpipe::runtime {

std::unique_ptr<cpipe_props_t> make_param_handle(nlohmann::json params) {
    auto handle = std::make_unique<cpipe_props_t>();
    handle->params = std::move(params);
    return handle;
}

const nlohmann::json* params_from_handle(const cpipe_props_t* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    return &handle->params;
}

}  // namespace cpipe::runtime
