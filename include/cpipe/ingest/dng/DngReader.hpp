// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace cpipe::ingest::dng {

struct DngReadResult {
    cpipe_status_t status{CPIPE_FAILED};
    std::shared_ptr<compute::IBuffer> buffer;
    std::string message;
};

class DngReader {
public:
    [[nodiscard]] static DngReadResult read(const std::filesystem::path& path);
};

}  // namespace cpipe::ingest::dng
