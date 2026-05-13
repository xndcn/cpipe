// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#if defined(__ELF__)
#define CPIPE_SECTION_PUT __attribute__((used, section("cpipe_registry")))
#else
#error "cpipe_registry section support is only enabled for Linux ELF in P0"
#endif
