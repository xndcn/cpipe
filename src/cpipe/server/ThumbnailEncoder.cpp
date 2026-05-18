// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <webp/encode.h>

#include <algorithm>
#include <cpipe/server/ThumbnailEncoder.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpipe::server {

std::vector<std::uint8_t> encode_placeholder_thumbnail_webp(std::uint32_t max_size, float quality) {
    const auto size = std::clamp<std::uint32_t>(max_size, 1U, 256U);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4U);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * size + x) * 4U;
            rgba[offset + 0U] = static_cast<std::uint8_t>((x * 255U) / size);
            rgba[offset + 1U] = static_cast<std::uint8_t>((y * 255U) / size);
            rgba[offset + 2U] = static_cast<std::uint8_t>(((x ^ y) * 255U) / size);
            rgba[offset + 3U] = 255U;
        }
    }

    std::uint8_t* encoded = nullptr;
    const auto byte_count =
        WebPEncodeRGBA(rgba.data(), static_cast<int>(size), static_cast<int>(size),
                       static_cast<int>(size * 4U), quality, &encoded);
    if (byte_count == 0U || encoded == nullptr) {
        return {};
    }

    std::vector<std::uint8_t> out(encoded, encoded + byte_count);
    WebPFree(encoded);
    return out;
}

}  // namespace cpipe::server
