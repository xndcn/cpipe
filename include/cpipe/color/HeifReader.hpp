// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cpipe::color {

struct HeifInfo {
    std::uint32_t width{0};
    std::uint32_t height{0};
    int luma_bits_per_pixel{0};
    std::size_t icc_profile_bytes{0};
    int nclx_color_primaries{0};
    int nclx_transfer_characteristics{0};
    int nclx_matrix_coefficients{0};
    std::vector<std::uint8_t> decoded_rgba;
    std::vector<float> scene_linear_rec2020_rgba;
};

struct HeifReadOptions {
    std::filesystem::path ocio_config_path;
};

[[nodiscard]] cpipe_status_t read_heif_sdr(const std::filesystem::path& path, HeifInfo* out,
                                           std::string* error);
[[nodiscard]] cpipe_status_t read_heif_sdr(const std::filesystem::path& path,
                                           const HeifReadOptions& options, HeifInfo* out,
                                           std::string* error);

}  // namespace cpipe::color
