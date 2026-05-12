// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class BufferHandle final {
public:
    explicit BufferHandle(compute::IBuffer& buffer);
    ~BufferHandle();

    BufferHandle(const BufferHandle&) = delete;
    auto operator=(const BufferHandle&) -> BufferHandle& = delete;

    BufferHandle(BufferHandle&&) = delete;
    auto operator=(BufferHandle&&) -> BufferHandle& = delete;

    [[nodiscard]] auto native() noexcept -> cpipe_buffer_t*;
    [[nodiscard]] auto native() const noexcept -> const cpipe_buffer_t*;

private:
    cpipe_buffer_t* native_ = nullptr;
};

}  // namespace cpipe::runtime
