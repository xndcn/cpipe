// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <vector>

namespace cpipe::server {

[[nodiscard]] std::vector<std::uint8_t> encode_placeholder_thumbnail_webp(std::uint32_t max_size,
                                                                          float quality);

}  // namespace cpipe::server
