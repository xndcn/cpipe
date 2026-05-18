// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/server/EditorProtocol.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kHeaderBytes = 13;

void write_be32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    out->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    out->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out->push_back(static_cast<std::uint8_t>(value & 0xffU));
}

std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::optional<cpipe::server::EditorFrameType> frame_type_from_byte(std::uint8_t value) {
    switch (value) {
        case static_cast<std::uint8_t>(cpipe::server::EditorFrameType::Thumbnail):
            return cpipe::server::EditorFrameType::Thumbnail;
        case static_cast<std::uint8_t>(cpipe::server::EditorFrameType::Profile):
            return cpipe::server::EditorFrameType::Profile;
        case static_cast<std::uint8_t>(cpipe::server::EditorFrameType::Log):
            return cpipe::server::EditorFrameType::Log;
        case static_cast<std::uint8_t>(cpipe::server::EditorFrameType::Ack):
            return cpipe::server::EditorFrameType::Ack;
        case static_cast<std::uint8_t>(cpipe::server::EditorFrameType::Control):
            return cpipe::server::EditorFrameType::Control;
        default:
            return std::nullopt;
    }
}

}  // namespace

namespace cpipe::server {

std::vector<std::uint8_t> encode_frame(EditorFrameType type, std::uint32_t node_id,
                                       std::uint32_t timestamp_ms, std::string_view payload) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderBytes + payload.size());
    out.push_back(static_cast<std::uint8_t>(type));
    write_be32(&out, node_id);
    write_be32(&out, timestamp_ms);
    write_be32(&out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::optional<EditorFrame> decode_frame(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderBytes) {
        return std::nullopt;
    }
    const auto type = frame_type_from_byte(bytes[0]);
    if (!type) {
        return std::nullopt;
    }
    const auto length = read_be32(bytes, 9U);
    if (bytes.size() != kHeaderBytes + length) {
        return std::nullopt;
    }
    EditorFrame frame;
    frame.type = *type;
    frame.node_id = read_be32(bytes, 1U);
    frame.timestamp_ms = read_be32(bytes, 5U);
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes), bytes.end());
    return frame;
}

std::optional<EditorFrame> decode_frame(std::string_view bytes) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return decode_frame(std::span<const std::uint8_t>{data, bytes.size()});
}

}  // namespace cpipe::server
