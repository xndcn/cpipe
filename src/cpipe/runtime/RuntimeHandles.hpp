// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <memory>
#include <nlohmann/json.hpp>

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"

struct cpipe_buffer_s {
    std::shared_ptr<cpipe::compute::IBuffer> buffer;
};

struct cpipe_compute_s {
    cpipe::runtime::ComputeContext* context = nullptr;
};

struct cpipe_inference_s {
    cpipe::runtime::InferenceContext* context = nullptr;
};

struct cpipe_props_s {
    nlohmann::json values;
};

struct cpipe_node_s {
    void* instance_state = nullptr;
};
