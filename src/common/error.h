// src/common/error.h -- internal error utilities (extends public cpipe/error.h)
#pragma once
#include <cpipe/error.h>
#include <string_view>

namespace cpipe {

/// Returns a human-readable name for a status code.
std::string_view status_to_string(cpipe_status_t code) noexcept;

/// Convenience: build an Error value.
inline Error make_error(cpipe_status_t code, std::string message) {
    return Error{code, std::move(message)};
}

} // namespace cpipe
