// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

int cpipe_abi_c_header_compile_check(void) {
    return CPIPE_ABI_MAJOR == 0 && CPIPE_ABI_MINOR == 1 ? CPIPE_OK : CPIPE_FAILED;
}
