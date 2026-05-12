// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/sdk/cpipe_node.h"

int main(void) {
    cpipe_plugin_desc_t desc;
    desc.abi_major = CPIPE_ABI_MAJOR;
    desc.abi_minor = CPIPE_ABI_MINOR;
    desc.node_id = "com.cpipe.test.c";
    desc.node_version = "0.1.0";
    desc.manifest_json = "{}";
    desc.main_entry = 0;
    return desc.abi_major == 0 ? 0 : 1;
}
