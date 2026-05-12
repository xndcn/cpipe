// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>

namespace cpipe {

enum class StatusCode {
    Ok = 0,
    Failed = 1,
    ReplyDefault = 2,
    OutOfMemory = 3,
    BadPrecision = 4,
    BadIndex = 5,
    NeedParam = 6,
    InternalError = 7,
    Unsupported = 8,
    InvalidArgument = 9,
    NotFound = 10,
};

std::string_view to_string(StatusCode status) noexcept;

struct Error {
    StatusCode code = StatusCode::Ok;
    std::string message;
};

template <typename T>
using Result = tl::expected<T, Error>;

inline tl::expected<void, Error> ok() noexcept {
    return {};
}

inline Error make_error(StatusCode code, std::string message) {
    return Error{code, std::move(message)};
}

}  // namespace cpipe
