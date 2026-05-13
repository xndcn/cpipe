// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace cpipe::color {

struct Rgba16ImageView {
    const std::uint16_t* pixels{nullptr};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride_pixels{0};
};

struct HeifWriteOptions {
    std::filesystem::path ocio_config_path;
    int quality{58};
};

[[nodiscard]] cpipe_status_t write_heif_sdr(const std::filesystem::path& path,
                                            const Rgba16ImageView& image,
                                            const HeifWriteOptions& options, std::string* error);

}  // namespace cpipe::color
