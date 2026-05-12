// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cpipe {

enum class StatusCode : std::uint32_t {
    Ok = 0,
    Failed = 1,
    ReplyDefault = 2,
    OutOfMemory = 3,
    BadPrecision = 4,
    BadIndex = 5,
    NeedParam = 6,
    InternalError = 7,
    Unsupported = 8,
};

[[nodiscard]] constexpr auto is_ok(StatusCode code) noexcept -> bool {
    return code == StatusCode::Ok;
}

[[nodiscard]] constexpr auto to_string(StatusCode code) noexcept -> std::string_view {
    switch (code) {
        case StatusCode::Ok:
            return "OK";
        case StatusCode::Failed:
            return "FAILED";
        case StatusCode::ReplyDefault:
            return "REPLY_DEFAULT";
        case StatusCode::OutOfMemory:
            return "OOM";
        case StatusCode::BadPrecision:
            return "BAD_PRECISION";
        case StatusCode::BadIndex:
            return "BAD_INDEX";
        case StatusCode::NeedParam:
            return "NEED_PARAM";
        case StatusCode::InternalError:
            return "INTERNAL_ERROR";
        case StatusCode::Unsupported:
            return "UNSUPPORTED";
    }

    return "UNKNOWN";
}

[[nodiscard]] constexpr auto status_code_from_uint32(std::uint32_t value) noexcept
    -> std::optional<StatusCode> {
    switch (value) {
        case 0:
            return StatusCode::Ok;
        case 1:
            return StatusCode::Failed;
        case 2:
            return StatusCode::ReplyDefault;
        case 3:
            return StatusCode::OutOfMemory;
        case 4:
            return StatusCode::BadPrecision;
        case 5:
            return StatusCode::BadIndex;
        case 6:
            return StatusCode::NeedParam;
        case 7:
            return StatusCode::InternalError;
        case 8:
            return StatusCode::Unsupported;
        default:
            return std::nullopt;
    }
}

}  // namespace cpipe
