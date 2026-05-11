// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cpipe {

enum class Status : std::uint32_t {
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

[[nodiscard]] constexpr std::uint32_t to_code(Status status) noexcept {
    return static_cast<std::uint32_t>(status);
}

[[nodiscard]] constexpr std::optional<Status> status_from_code(std::uint32_t code) noexcept {
    switch (code) {
        case 0:
            return Status::Ok;
        case 1:
            return Status::Failed;
        case 2:
            return Status::ReplyDefault;
        case 3:
            return Status::OutOfMemory;
        case 4:
            return Status::BadPrecision;
        case 5:
            return Status::BadIndex;
        case 6:
            return Status::NeedParam;
        case 7:
            return Status::InternalError;
        case 8:
            return Status::Unsupported;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] constexpr std::string_view to_string(Status status) noexcept {
    switch (status) {
        case Status::Ok:
            return "CPIPE_OK";
        case Status::Failed:
            return "CPIPE_FAILED";
        case Status::ReplyDefault:
            return "CPIPE_REPLY_DEFAULT";
        case Status::OutOfMemory:
            return "CPIPE_OOM";
        case Status::BadPrecision:
            return "CPIPE_BAD_PRECISION";
        case Status::BadIndex:
            return "CPIPE_BAD_INDEX";
        case Status::NeedParam:
            return "CPIPE_NEED_PARAM";
        case Status::InternalError:
            return "CPIPE_INTERNAL_ERROR";
        case Status::Unsupported:
            return "CPIPE_UNSUPPORTED";
    }
    return "CPIPE_UNKNOWN";
}

}  // namespace cpipe
