// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/BufferLayout.hpp>

namespace cpipe::compute {

std::uint64_t BufferLayout::size_bytes() const noexcept {
    if (kind == BufferKind::Blob) {
        return dims[0];
    }

    if (ndim == 0 || ndim > 8) {
        return 0;
    }

    const auto element_bytes = bytes_per_pixel(format);
    if (element_bytes == 0) {
        return 0;
    }

    if (stride[ndim - 1] != 0) {
        return stride[ndim - 1] * dims[ndim - 1];
    }

    std::uint64_t total = element_bytes;
    for (std::uint8_t i = 0; i < ndim; ++i) {
        total *= dims[i];
    }
    return total;
}

}  // namespace cpipe::compute
