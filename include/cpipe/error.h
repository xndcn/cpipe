// include/cpipe/error.h -- Public error type and expected<T,E> alias
#pragma once
#include <cpipe/types.h>
#include <string>

// Polyfill: use std::expected (C++23) if available, otherwise tl::expected
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>
namespace cpipe {
template <typename T, typename E>
using expected = std::expected<T, E>;
template <typename E>
using unexpected = std::unexpected<E>;
} // namespace cpipe
#else
#include <tl/expected.hpp>
namespace cpipe {
template <typename T, typename E>
using expected = tl::expected<T, E>;
template <typename E>
using unexpected = tl::unexpected<E>;
} // namespace cpipe
#endif

namespace cpipe {

struct Error {
    cpipe_status_t code;
    std::string    message;
};

} // namespace cpipe
