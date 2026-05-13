// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <memory>

struct cpipe_buffer_s {
    std::shared_ptr<cpipe::compute::IBuffer> buffer;
    cpipe_metadata_t metadata_view;
};

namespace cpipe::runtime {

[[nodiscard]] std::unique_ptr<cpipe_buffer_t> make_buffer_handle(
    std::shared_ptr<compute::IBuffer> buffer);

[[nodiscard]] std::shared_ptr<compute::IBuffer> buffer_from_handle(const cpipe_buffer_t* handle);

}  // namespace cpipe::runtime
