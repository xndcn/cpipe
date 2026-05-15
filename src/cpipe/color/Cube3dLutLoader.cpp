// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <cctype>
#include <cpipe/color/Cube3dLutLoader.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cpipe::color {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::string lower_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string strip_comment(std::string line) {
    const auto pos = line.find('#');
    if (pos != std::string::npos) {
        line.resize(pos);
    }
    return line;
}

std::size_t value_count_for_size(std::uint32_t size) {
    const auto extent = static_cast<std::size_t>(size);
    return extent * extent * extent * 3U;
}

bool valid_size(std::uint32_t size, std::string* error) {
    if (size < 2U) {
        set_error(error, "3D LUT size must be at least 2");
        return false;
    }
    if (size > 256U) {
        set_error(error, "3D LUT size above P2 loader limit");
        return false;
    }
    return true;
}

cpipe_status_t load_cube(std::istream& input, Cube3dLut* out, std::string* error) {
    std::uint32_t size = 0;
    std::vector<float> values;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row{strip_comment(line)};
        std::string head;
        if (!(row >> head)) {
            continue;
        }
        if (head == "TITLE" || head == "DOMAIN_MIN" || head == "DOMAIN_MAX") {
            continue;
        }
        if (head == "LUT_3D_SIZE") {
            int parsed_size = 0;
            if (!(row >> parsed_size) || parsed_size < 0) {
                set_error(error, "invalid LUT_3D_SIZE");
                return CPIPE_FAILED;
            }
            size = static_cast<std::uint32_t>(parsed_size);
            if (!valid_size(size, error)) {
                return CPIPE_FAILED;
            }
            values.reserve(value_count_for_size(size));
            continue;
        }

        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        std::istringstream values_row{strip_comment(line)};
        if (!(values_row >> r >> g >> b)) {
            set_error(error, "invalid .cube RGB row");
            return CPIPE_FAILED;
        }
        values.push_back(r);
        values.push_back(g);
        values.push_back(b);
    }

    if (size == 0U) {
        set_error(error, "missing LUT_3D_SIZE");
        return CPIPE_FAILED;
    }
    if (values.size() != value_count_for_size(size)) {
        set_error(error, "wrong number of .cube RGB rows");
        return CPIPE_FAILED;
    }

    out->size = size;
    out->values = std::move(values);
    return CPIPE_OK;
}

cpipe_status_t load_spi3d(std::istream& input, Cube3dLut* out, std::string* error) {
    std::string magic;
    double version = 0.0;
    if (!(input >> magic >> version) || magic != "SPILUT") {
        set_error(error, "missing SPILUT header");
        return CPIPE_FAILED;
    }

    int input_dims = 0;
    int output_dims = 0;
    if (!(input >> input_dims >> output_dims) || input_dims != 3 || output_dims != 3) {
        set_error(error, "unsupported .spi3d component count");
        return CPIPE_FAILED;
    }

    int r_size = 0;
    int g_size = 0;
    int b_size = 0;
    if (!(input >> r_size >> g_size >> b_size) || r_size != g_size || g_size != b_size ||
        r_size < 0) {
        set_error(error, "unsupported .spi3d grid dimensions");
        return CPIPE_FAILED;
    }
    const auto size = static_cast<std::uint32_t>(r_size);
    if (!valid_size(size, error)) {
        return CPIPE_FAILED;
    }

    std::vector<float> values(value_count_for_size(size), 0.0F);
    std::vector<char> seen(value_count_for_size(size) / 3U, 0);
    int r = 0;
    int g = 0;
    int b = 0;
    float out_r = 0.0F;
    float out_g = 0.0F;
    float out_b = 0.0F;
    while (input >> r >> g >> b >> out_r >> out_g >> out_b) {
        if (r < 0 || g < 0 || b < 0 || r >= r_size || g >= g_size || b >= b_size) {
            set_error(error, ".spi3d index out of range");
            return CPIPE_FAILED;
        }
        const auto index = (static_cast<std::size_t>(r) * size * size) +
                           (static_cast<std::size_t>(g) * size) + static_cast<std::size_t>(b);
        const auto offset = index * 3U;
        values[offset] = out_r;
        values[offset + 1U] = out_g;
        values[offset + 2U] = out_b;
        seen[index] = 1;
    }

    if (!std::ranges::all_of(seen, [](char value) { return value != 0; })) {
        set_error(error, "missing .spi3d sample rows");
        return CPIPE_FAILED;
    }

    out->size = size;
    out->values = std::move(values);
    return CPIPE_OK;
}

}  // namespace

std::array<float, 3> Cube3dLut::at(std::uint32_t r, std::uint32_t g, std::uint32_t b) const {
    const auto offset =
        ((static_cast<std::size_t>(r) * size * size) + (static_cast<std::size_t>(g) * size) + b) *
        3U;
    return {values[offset], values[offset + 1U], values[offset + 2U]};
}

cpipe_status_t Cube3dLutLoader::load(const std::filesystem::path& path, Cube3dLut* out,
                                     std::string* error) {
    if (out == nullptr) {
        set_error(error, "output LUT pointer is null");
        return CPIPE_BAD_INDEX;
    }
    *out = Cube3dLut{};

    std::ifstream input{path};
    if (!input) {
        set_error(error, "failed to open 3D LUT file");
        return CPIPE_FAILED;
    }

    const auto ext = lower_extension(path);
    if (ext == ".cube") {
        return load_cube(input, out, error);
    }
    if (ext == ".spi3d") {
        return load_spi3d(input, out, error);
    }

    set_error(error, "unsupported 3D LUT extension");
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::color
