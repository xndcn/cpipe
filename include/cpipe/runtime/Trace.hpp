// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#if defined(CPIPE_ENABLE_TRACY) && CPIPE_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#define CPIPE_TRACE_SCOPE(name) ZoneScopedN(name)
#else
#define CPIPE_TRACE_SCOPE(name) static_cast<void>(0)
#endif
