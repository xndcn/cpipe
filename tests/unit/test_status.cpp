// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/Status.hpp>
#include <cstdint>
#include <string_view>

using cpipe::StatusCode;

TEST_CASE("StatusCode names and numeric values are stable") {
    constexpr std::array kCodes = {
        StatusCode::Ok,          StatusCode::Failed,        StatusCode::ReplyDefault,
        StatusCode::OutOfMemory, StatusCode::BadPrecision,  StatusCode::BadIndex,
        StatusCode::NeedParam,   StatusCode::InternalError, StatusCode::Unsupported,
    };

    for (const auto code : kCodes) {
        const auto numeric = static_cast<std::uint32_t>(code);
        REQUIRE(cpipe::status_code_from_uint32(numeric) == code);
        REQUIRE(cpipe::to_string(code) != std::string_view{"UNKNOWN"});
    }

    REQUIRE(cpipe::is_ok(StatusCode::Ok));
    REQUIRE_FALSE(cpipe::is_ok(StatusCode::Failed));
    REQUIRE(cpipe::status_code_from_uint32(99U) == std::nullopt);
}
