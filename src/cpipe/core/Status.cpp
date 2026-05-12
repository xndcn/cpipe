// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/core/Status.hpp"

namespace cpipe {

std::string_view to_string(StatusCode status) noexcept {
    switch (status) {
        case StatusCode::Ok:
            return "CPIPE_OK";
        case StatusCode::Failed:
            return "CPIPE_FAILED";
        case StatusCode::ReplyDefault:
            return "CPIPE_REPLY_DEFAULT";
        case StatusCode::OutOfMemory:
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
        case StatusCode::InvalidArgument:
            return "CPIPE_INVALID_ARGUMENT";
        case StatusCode::NotFound:
            return "CPIPE_NOT_FOUND";
    }
    return "CPIPE_UNKNOWN";
}

}  // namespace cpipe
