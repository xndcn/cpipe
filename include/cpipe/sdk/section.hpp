// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#if defined(__linux__) || defined(__ELF__)
#define CPIPE_REGISTRY_SECTION __attribute__((used, aligned(8), section("cpipe_registry")))
#else
#error "P0 only enables the Linux ELF cpipe registry section."
#endif
