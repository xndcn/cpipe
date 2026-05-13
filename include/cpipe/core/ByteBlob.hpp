// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <vector>

namespace cpipe::compute {

struct ByteBlob {
    std::vector<std::byte> bytes;
};

}  // namespace cpipe::compute
