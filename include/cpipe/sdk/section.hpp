// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include "cpipe/sdk/cpipe_node.h"

#if defined(__ELF__)
#define CPIPE_SECTION_PUT [[gnu::used, gnu::section("cpipe_registry")]]

extern "C" {
// Linker-defined ELF section bounds intentionally use reserved __start/__stop names.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern const cpipe_plugin_desc_t __start_cpipe_registry[] __attribute__((weak));
extern const cpipe_plugin_desc_t __stop_cpipe_registry[] __attribute__((weak));
// NOLINTEND(bugprone-reserved-identifier)
}

namespace cpipe::sdk::detail {

[[nodiscard]] inline auto registry_section_begin() noexcept -> const cpipe_plugin_desc_t* {
    return &__start_cpipe_registry[0];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
}

[[nodiscard]] inline auto registry_section_end() noexcept -> const cpipe_plugin_desc_t* {
    return &__stop_cpipe_registry[0];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
}

}  // namespace cpipe::sdk::detail

#else
#error "cpipe_registry linker section is implemented for Linux ELF in P0"
#endif
