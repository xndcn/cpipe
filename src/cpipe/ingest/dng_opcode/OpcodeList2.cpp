// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <bit>
#include <cmath>
#include <cpipe/ingest/dng_opcode/OpcodeList2.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace cpipe::ingest::dng_opcode {
namespace {

constexpr std::uint32_t kOpcodeGainMap = 9;
constexpr std::size_t kOpcodeHeaderBytes = 16;
constexpr std::size_t kGainMapHeaderBytes = 76;

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t offset{0};

    [[nodiscard]] bool can_read(std::size_t size) const {
        return offset <= bytes.size() && size <= bytes.size() - offset;
    }

    [[nodiscard]] std::uint32_t read_u32() {
        const auto value = (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
                           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                           std::to_integer<std::uint32_t>(bytes[offset + 3U]);
        offset += 4U;
        return value;
    }

    [[nodiscard]] std::uint64_t read_u64() {
        const auto high = static_cast<std::uint64_t>(read_u32());
        const auto low = static_cast<std::uint64_t>(read_u32());
        return (high << 32U) | low;
    }

    [[nodiscard]] double read_double() {
        return std::bit_cast<double>(read_u64());
    }

    [[nodiscard]] float read_float() {
        return std::bit_cast<float>(read_u32());
    }
};

[[nodiscard]] bool checked_gain_count(const GainMap& map, std::size_t* out) {
    const auto v = static_cast<std::uint64_t>(map.map_points_v);
    const auto h = static_cast<std::uint64_t>(map.map_points_h);
    const auto planes = static_cast<std::uint64_t>(map.map_planes);
    if (map.map_points_v == 0 || map.map_points_h == 0 || map.map_planes == 0 ||
        v > std::numeric_limits<std::uint64_t>::max() / h) {
        return false;
    }
    const auto vh = v * h;
    if (vh > std::numeric_limits<std::uint64_t>::max() / planes) {
        return false;
    }
    const auto total = vh * planes;
    if (total > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    *out = static_cast<std::size_t>(total);
    return true;
}

[[nodiscard]] bool valid_required_shape(const GainMap& map) {
    return (map.planes == 1 || map.planes == 4) && map.plane < map.planes && map.map_planes == 1 &&
           map.row_pitch > 0 && map.col_pitch > 0 && std::isfinite(map.map_spacing_v) &&
           std::isfinite(map.map_spacing_h) && map.map_spacing_v > 0.0 && map.map_spacing_h > 0.0 &&
           std::isfinite(map.map_origin_v) && std::isfinite(map.map_origin_h);
}

[[nodiscard]] GainMapParseResult failed(std::string message) {
    GainMapParseResult result{};
    result.status = CPIPE_FAILED;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] GainMapParseResult unsupported(std::string message) {
    GainMapParseResult result{};
    result.status = CPIPE_UNSUPPORTED;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] GainMapParseResult parse_gain_map(std::span<const std::byte> params,
                                                std::vector<GainMap>* maps) {
    if (params.size() < kGainMapHeaderBytes) {
        return failed("GainMap params truncated");
    }

    Reader reader{params};
    GainMap map{};
    map.top = reader.read_u32();
    map.left = reader.read_u32();
    map.bottom = reader.read_u32();
    map.right = reader.read_u32();
    map.plane = reader.read_u32();
    map.planes = reader.read_u32();
    map.row_pitch = reader.read_u32();
    map.col_pitch = reader.read_u32();
    map.map_points_v = reader.read_u32();
    map.map_points_h = reader.read_u32();
    map.map_spacing_v = reader.read_double();
    map.map_spacing_h = reader.read_double();
    map.map_origin_v = reader.read_double();
    map.map_origin_h = reader.read_double();
    map.map_planes = reader.read_u32();

    std::size_t gain_count = 0;
    if (!checked_gain_count(map, &gain_count)) {
        return failed("GainMap size overflows");
    }
    if (gain_count >
        (std::numeric_limits<std::size_t>::max() - kGainMapHeaderBytes) / sizeof(float)) {
        return failed("GainMap byte size overflows");
    }
    const auto expected_size = kGainMapHeaderBytes + gain_count * sizeof(float);
    if (params.size() < expected_size) {
        return failed("GainMap gain table truncated");
    }
    if (!valid_required_shape(map)) {
        return unsupported("GainMap shape is outside cpipe v1 support");
    }

    map.gain.reserve(gain_count);
    for (std::size_t i = 0; i < gain_count; ++i) {
        map.gain.push_back(reader.read_float());
    }
    maps->push_back(std::move(map));
    GainMapParseResult result{};
    result.status = CPIPE_OK;
    return result;
}

}  // namespace

GainMapParseResult OpcodeList2::parse_gain_maps(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        GainMapParseResult result{};
        result.status = CPIPE_OK;
        return result;
    }
    Reader reader{bytes};
    if (!reader.can_read(sizeof(std::uint32_t))) {
        return failed("OpcodeList2 count truncated");
    }
    const auto count = reader.read_u32();
    std::vector<GainMap> maps;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!reader.can_read(kOpcodeHeaderBytes)) {
            return failed("OpcodeList2 opcode header truncated");
        }
        const auto opcode_id = reader.read_u32();
        static_cast<void>(reader.read_u32());
        static_cast<void>(reader.read_u32());
        const auto param_size = reader.read_u32();
        if (!reader.can_read(param_size)) {
            return failed("OpcodeList2 opcode params truncated");
        }
        const auto params = bytes.subspan(reader.offset, param_size);
        reader.offset += param_size;
        if (opcode_id != kOpcodeGainMap) {
            continue;
        }
        auto parsed = parse_gain_map(params, &maps);
        if (parsed.status != CPIPE_OK) {
            return parsed;
        }
    }

    GainMapParseResult result{};
    result.status = CPIPE_OK;
    result.gain_maps = std::move(maps);
    return result;
}

}  // namespace cpipe::ingest::dng_opcode
