// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cpipe::server {

enum class EditorFrameType : std::uint8_t {
    Thumbnail = 0x01,
    Profile = 0x02,
    Log = 0x03,
    Ack = 0x04,
    Control = 0x10,
};

struct EditorFrame {
    EditorFrameType type{EditorFrameType::Control};
    std::uint32_t node_id{0};
    std::uint32_t timestamp_ms{0};
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> encode_frame(EditorFrameType type, std::uint32_t node_id,
                                                     std::uint32_t timestamp_ms,
                                                     std::string_view payload);
[[nodiscard]] std::optional<EditorFrame> decode_frame(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::optional<EditorFrame> decode_frame(std::string_view bytes);

}  // namespace cpipe::server
