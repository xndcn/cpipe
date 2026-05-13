// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/BufferHandle.hpp>

namespace cpipe::runtime {

std::unique_ptr<cpipe_buffer_t> make_buffer_handle(std::shared_ptr<compute::IBuffer> buffer) {
    auto handle = std::make_unique<cpipe_buffer_t>();
    handle->buffer = std::move(buffer);
    return handle;
}

std::shared_ptr<compute::IBuffer> buffer_from_handle(const cpipe_buffer_t* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    return handle->buffer;
}

}  // namespace cpipe::runtime
