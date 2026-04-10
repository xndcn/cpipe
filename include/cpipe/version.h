#pragma once

#include <string_view>

namespace cpipe {

/// Returns the version string in "major.minor.patch" format.
std::string_view version_string() noexcept;

} // namespace cpipe
