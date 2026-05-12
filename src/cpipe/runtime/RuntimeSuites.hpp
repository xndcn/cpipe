// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime::detail {

[[nodiscard]] auto buffer_suite() noexcept -> const cpipe_buffer_suite_v1*;
[[nodiscard]] auto compute_suite() noexcept -> const cpipe_compute_suite_v1*;
[[nodiscard]] auto inference_suite() noexcept -> const cpipe_inference_suite_v1*;

}  // namespace cpipe::runtime::detail
