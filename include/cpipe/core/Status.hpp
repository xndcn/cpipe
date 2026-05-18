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
    Oom = 3,
    BadPrecision = 4,
    BadIndex = 5,
    NeedParam = 6,
    InternalError = 7,
    Unsupported = 8,
    NeedMetadata = 9,
    NotImplemented = 100,
};

[[nodiscard]] constexpr bool is_ok(StatusCode code) noexcept {
    return code == StatusCode::Ok;
}

[[nodiscard]] constexpr std::string_view to_string(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok:
            return "CPIPE_OK";
        case StatusCode::Failed:
            return "CPIPE_FAILED";
        case StatusCode::ReplyDefault:
            return "CPIPE_REPLY_DEFAULT";
        case StatusCode::Oom:
            return "CPIPE_OOM";
        case StatusCode::BadPrecision:
            return "CPIPE_BAD_PRECISION";
        case StatusCode::BadIndex:
            return "CPIPE_BAD_INDEX";
        case StatusCode::NeedParam:
            return "CPIPE_NEED_PARAM";
        case StatusCode::InternalError:
            return "CPIPE_INTERNAL_ERROR";
        case StatusCode::Unsupported:
            return "CPIPE_UNSUPPORTED";
        case StatusCode::NeedMetadata:
            return "CPIPE_NEED_METADATA";
        case StatusCode::NotImplemented:
            return "CPIPE_NOT_IMPLEMENTED";
    }
    return "CPIPE_UNKNOWN";
}

}  // namespace cpipe::compute
