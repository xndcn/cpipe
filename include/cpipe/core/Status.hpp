// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <string_view>

namespace cpipe::compute {

enum class StatusCode : std::uint8_t {
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

constexpr auto to_string(StatusCode status) noexcept -> std::string_view {
    switch (status) {
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

}  // namespace cpipe::compute
