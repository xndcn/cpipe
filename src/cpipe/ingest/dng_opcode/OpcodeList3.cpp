// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <bit>
#include <cpipe/ingest/dng_opcode/OpcodeList3.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace cpipe::ingest::dng_opcode {
namespace {

constexpr std::size_t kOpcodeHeaderBytes = 16;
constexpr std::uint32_t kOptionalFlag = 1;
constexpr std::uint32_t kMaxWarpPlanes = 4;

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
};

[[nodiscard]] OpcodeList3::ParseResult failed(std::string message) {
    OpcodeList3::ParseResult result{};
    result.status = CPIPE_FAILED;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] OpcodeList3::ParseResult unsupported(std::string message) {
    OpcodeList3::ParseResult result{};
    result.status = CPIPE_UNSUPPORTED;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] bool exact_size(std::span<const std::byte> params, std::size_t expected) {
    return params.size() == expected;
}

[[nodiscard]] OpcodeList3::OpcodeId opcode_id_from_raw(std::uint32_t raw_id) {
    switch (raw_id) {
        case static_cast<std::uint32_t>(OpcodeList3::OpcodeId::WarpRectilinear):
            return OpcodeList3::OpcodeId::WarpRectilinear;
        case static_cast<std::uint32_t>(OpcodeList3::OpcodeId::FixVignetteRadial):
            return OpcodeList3::OpcodeId::FixVignetteRadial;
        case static_cast<std::uint32_t>(OpcodeList3::OpcodeId::FixBadPixelsConstant):
            return OpcodeList3::OpcodeId::FixBadPixelsConstant;
        case static_cast<std::uint32_t>(OpcodeList3::OpcodeId::FixBadPixelsList):
            return OpcodeList3::OpcodeId::FixBadPixelsList;
        case static_cast<std::uint32_t>(OpcodeList3::OpcodeId::TrimBounds):
            return OpcodeList3::OpcodeId::TrimBounds;
        default:
            return OpcodeList3::OpcodeId::Unknown;
    }
}

[[nodiscard]] bool parse_warp_rectilinear(std::span<const std::byte> params,
                                          OpcodeList3::Opcode* opcode) {
    if (params.size() < sizeof(std::uint32_t)) {
        return false;
    }
    Reader reader{params};
    const auto count = reader.read_u32();
    if (count == 0U || count > kMaxWarpPlanes) {
        return false;
    }
    const auto coefficient_bytes = static_cast<std::size_t>(count) * 6U * sizeof(double);
    const auto expected_size = sizeof(std::uint32_t) + coefficient_bytes + (2U * sizeof(double));
    if (!exact_size(params, expected_size)) {
        return false;
    }

    OpcodeList3::WarpRectilinear warp{};
    warp.coefficients.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        OpcodeList3::WarpCoefficient coefficient{};
        coefficient.kr0 = reader.read_double();
        coefficient.kr1 = reader.read_double();
        coefficient.kr2 = reader.read_double();
        coefficient.kr3 = reader.read_double();
        coefficient.kt0 = reader.read_double();
        coefficient.kt1 = reader.read_double();
        warp.coefficients.push_back(coefficient);
    }
    warp.cx_hat = reader.read_double();
    warp.cy_hat = reader.read_double();
    opcode->warp_rectilinear = std::move(warp);
    return true;
}

[[nodiscard]] bool parse_fix_vignette_radial(std::span<const std::byte> params,
                                             OpcodeList3::Opcode* opcode) {
    if (!exact_size(params, 7U * sizeof(double))) {
        return false;
    }
    Reader reader{params};
    OpcodeList3::FixVignetteRadial vignette{};
    for (auto& k : vignette.k) {
        k = reader.read_double();
    }
    vignette.cx_hat = reader.read_double();
    vignette.cy_hat = reader.read_double();
    opcode->fix_vignette_radial = vignette;
    return true;
}

[[nodiscard]] bool parse_fix_bad_pixels_constant(std::span<const std::byte> params,
                                                 OpcodeList3::Opcode* opcode) {
    if (!exact_size(params, 2U * sizeof(std::uint32_t))) {
        return false;
    }
    Reader reader{params};
    OpcodeList3::FixBadPixelsConstant constant{};
    constant.constant = reader.read_u32();
    constant.bayer_phase = reader.read_u32();
    opcode->fix_bad_pixels_constant = constant;
    return true;
}

[[nodiscard]] bool checked_list_size(std::uint32_t points, std::uint32_t rects,
                                     std::size_t* expected) {
    constexpr auto header_bytes = 3U * sizeof(std::uint32_t);
    constexpr auto point_bytes = 2U * sizeof(std::uint32_t);
    constexpr auto rect_bytes = 4U * sizeof(std::uint32_t);
    const auto point_count = static_cast<std::size_t>(points);
    const auto rect_count = static_cast<std::size_t>(rects);
    if (point_count > (std::numeric_limits<std::size_t>::max() - header_bytes) / point_bytes) {
        return false;
    }
    const auto with_points = header_bytes + (point_count * point_bytes);
    if (rect_count > (std::numeric_limits<std::size_t>::max() - with_points) / rect_bytes) {
        return false;
    }
    *expected = with_points + (rect_count * rect_bytes);
    return true;
}

[[nodiscard]] bool parse_fix_bad_pixels_list(std::span<const std::byte> params,
                                             OpcodeList3::Opcode* opcode) {
    if (params.size() < 3U * sizeof(std::uint32_t)) {
        return false;
    }
    Reader reader{params};
    OpcodeList3::FixBadPixelsList list{};
    list.bayer_phase = reader.read_u32();
    const auto point_count = reader.read_u32();
    const auto rect_count = reader.read_u32();

    std::size_t expected_size = 0;
    if (!checked_list_size(point_count, rect_count, &expected_size) ||
        !exact_size(params, expected_size)) {
        return false;
    }

    list.bad_points.reserve(point_count);
    for (std::uint32_t i = 0; i < point_count; ++i) {
        list.bad_points.push_back(
            OpcodeList3::BadPoint{.row = reader.read_u32(), .column = reader.read_u32()});
    }
    list.bad_rects.reserve(rect_count);
    for (std::uint32_t i = 0; i < rect_count; ++i) {
        list.bad_rects.push_back(OpcodeList3::BadRect{.top = reader.read_u32(),
                                                      .left = reader.read_u32(),
                                                      .bottom = reader.read_u32(),
                                                      .right = reader.read_u32()});
    }
    opcode->fix_bad_pixels_list = std::move(list);
    return true;
}

[[nodiscard]] bool parse_trim_bounds(std::span<const std::byte> params,
                                     OpcodeList3::Opcode* opcode) {
    if (!exact_size(params, 4U * sizeof(std::uint32_t))) {
        return false;
    }
    Reader reader{params};
    opcode->trim_bounds = OpcodeList3::TrimBounds{.top = reader.read_u32(),
                                                  .left = reader.read_u32(),
                                                  .bottom = reader.read_u32(),
                                                  .right = reader.read_u32()};
    return true;
}

[[nodiscard]] bool parse_known_opcode(std::span<const std::byte> params,
                                      OpcodeList3::Opcode* opcode) {
    switch (opcode->id) {
        case OpcodeList3::OpcodeId::WarpRectilinear:
            return parse_warp_rectilinear(params, opcode);
        case OpcodeList3::OpcodeId::FixVignetteRadial:
            return parse_fix_vignette_radial(params, opcode);
        case OpcodeList3::OpcodeId::FixBadPixelsConstant:
            return parse_fix_bad_pixels_constant(params, opcode);
        case OpcodeList3::OpcodeId::FixBadPixelsList:
            return parse_fix_bad_pixels_list(params, opcode);
        case OpcodeList3::OpcodeId::TrimBounds:
            return parse_trim_bounds(params, opcode);
        case OpcodeList3::OpcodeId::Unknown:
            break;
    }
    return false;
}

}  // namespace

OpcodeList3::ParseResult OpcodeList3::parse(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        OpcodeList3::ParseResult result{};
        result.status = CPIPE_OK;
        return result;
    }

    Reader reader{bytes};
    if (!reader.can_read(sizeof(std::uint32_t))) {
        return failed("OpcodeList3 count truncated");
    }

    const auto count = reader.read_u32();
    std::vector<Opcode> opcodes;
    opcodes.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!reader.can_read(kOpcodeHeaderBytes)) {
            return failed("OpcodeList3 opcode header truncated");
        }
        Opcode opcode{};
        opcode.raw_id = reader.read_u32();
        opcode.id = opcode_id_from_raw(opcode.raw_id);
        opcode.dng_version = reader.read_u32();
        opcode.flags = reader.read_u32();
        opcode.optional = (opcode.flags & kOptionalFlag) != 0U;
        const auto param_size = reader.read_u32();
        if (!reader.can_read(param_size)) {
            return failed("OpcodeList3 opcode params truncated");
        }
        const auto params = bytes.subspan(reader.offset, param_size);
        reader.offset += param_size;
        opcode.raw_params.assign(params.begin(), params.end());

        if (opcode.id == OpcodeId::Unknown) {
            if (!opcode.optional) {
                return unsupported("OpcodeList3 mandatory unknown opcode");
            }
            opcodes.push_back(std::move(opcode));
            continue;
        }
        if (!parse_known_opcode(params, &opcode)) {
            return failed("OpcodeList3 known opcode params invalid");
        }
        opcodes.push_back(std::move(opcode));
    }

    OpcodeList3::ParseResult result{};
    result.status = CPIPE_OK;
    result.opcodes = std::move(opcodes);
    return result;
}

}  // namespace cpipe::ingest::dng_opcode
