// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>

#include <vector>

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/core/Status.hpp"

namespace cpipe::runtime {

class HalideBufferAdapter {
public:
    static Result<HalideBufferAdapter> from_buffer(compute::IBuffer& buffer);

    [[nodiscard]] halide_buffer_t* get() noexcept;
    [[nodiscard]] const halide_buffer_t* get() const noexcept;

private:
    halide_buffer_t buffer_{};
    std::vector<halide_dimension_t> dims_;
};

}  // namespace cpipe::runtime
