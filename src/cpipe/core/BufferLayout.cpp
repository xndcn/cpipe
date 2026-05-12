// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/core/BufferLayout.hpp"

#include <array>
#include <cstdint>

namespace cpipe::compute {

auto BufferLayout::size_bytes() const noexcept -> std::uint64_t {
    if (ndim == 0 || ndim > dims.size()) {
        return 0;
    }

    if (kind == BufferKind::Blob) {
        return dims[0];
    }

    const auto maybe_element_bytes = bytes_per_pixel(format);
    if (!maybe_element_bytes.has_value()) {
        return 0;
    }

    std::array<std::uint64_t, 8> effective_stride{};
    effective_stride[0] = stride[0] == 0 ? maybe_element_bytes.value() : stride[0];
    if (dims[0] == 0) {
        return 0;
    }

    for (std::uint8_t i = 1; i < ndim; ++i) {
        if (dims[i] == 0) {
            return 0;
        }
        effective_stride[i] = stride[i] == 0 ? effective_stride[i - 1U] * dims[i - 1U] : stride[i];
    }

    return effective_stride[ndim - 1U] * dims[ndim - 1U];
}

}  // namespace cpipe::compute
