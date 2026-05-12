// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#define CPIPE_REGISTRY_SECTION_NAME "cpipe_registry"

#if (defined(__GNUC__) || defined(__clang__)) && defined(__ELF__)
#define CPIPE_SECTION_PUT(section_name) __attribute__((used, section(section_name)))
#else
#error "cpipe P0 only implements Linux ELF linker-section registration"
#endif

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)
