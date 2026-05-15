// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <lcms2.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/IccV4_4Writer.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset + 0U]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::uint32_t fourcc(std::string_view value) {
    return (static_cast<std::uint32_t>(value[0]) << 24U) |
           (static_cast<std::uint32_t>(value[1]) << 16U) |
           (static_cast<std::uint32_t>(value[2]) << 8U) | static_cast<std::uint32_t>(value[3]);
}

std::span<const std::uint8_t> tag_payload(std::span<const std::uint8_t> profile,
                                          std::string_view tag) {
    const auto tag_count = read_u32_be(profile, 128U);
    for (std::uint32_t i = 0; i < tag_count; ++i) {
        const auto record = 132U + (static_cast<std::size_t>(i) * 12U);
        if (read_u32_be(profile, record) == fourcc(tag)) {
            const auto offset = read_u32_be(profile, record + 4U);
            const auto size = read_u32_be(profile, record + 8U);
            REQUIRE(static_cast<std::size_t>(offset) + size <= profile.size());
            return profile.subspan(offset, size);
        }
    }
    return {};
}

}  // namespace

TEST_CASE("IccV4_4Writer emits lcms-loadable BT.2020 PQ ICC with CICP and A2B0") {
    const auto profile = cpipe::color::IccV4_4Writer::bt2020_pq();
    REQUIRE(profile.size() > 128U);
    REQUIRE(read_u32_be(profile, 0U) == profile.size());
    REQUIRE(profile[8] == 0x04U);
    REQUIRE(profile[9] == 0x40U);
    REQUIRE(std::memcmp(profile.data() + 36U, "acsp", 4U) == 0);

    std::unique_ptr<void, decltype(&cmsCloseProfile)> lcms{
        cmsOpenProfileFromMem(profile.data(), static_cast<cmsUInt32Number>(profile.size())),
        &cmsCloseProfile};
    REQUIRE(lcms != nullptr);

    const auto cicp = tag_payload(profile, "cicp");
    REQUIRE(cicp.size() >= 12U);
    REQUIRE(std::memcmp(cicp.data(), "cicp", 4U) == 0);
    REQUIRE(cicp[8] == 9U);
    REQUIRE(cicp[9] == 16U);
    REQUIRE(cicp[10] == 9U);
    REQUIRE(cicp[11] == 1U);

    const auto a2b0 = tag_payload(profile, "A2B0");
    REQUIRE(a2b0.size() > 16U);
    REQUIRE(std::memcmp(a2b0.data(), "mABF", 4U) == 0);
}
