// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#if defined(__ELF__)
#define CPIPE_SECTION_PUT __attribute__((used, section("cpipe_registry"), aligned(sizeof(void*))))
#else
#error "Phase 0 only supports Linux ELF cpipe_registry sections"
#endif
