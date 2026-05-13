// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenImageIO/imagebuf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Generates deterministic P1 self-referenced fixtures for the classic raw
// stages covered by docs/research/07-classic-isp-algorithms.md §3.3 / §3.9 and
// the matrix chain covered by docs/research/13-color-management.md §3.6.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> pixels;
};

constexpr std::array<float, 9> kD50ToD65{
    0.9555766F, -0.0230393F, 0.0631636F,  -0.0282895F, 1.0099416F,
    0.0210077F, 0.0122982F,  -0.0204830F, 1.3299098F,
};

constexpr std::array<float, 9> kXyzD65ToRec2020{
    1.7166512F, -0.3556708F, -0.2533663F, -0.6666844F, 1.6164812F,
    0.0157685F, 0.0176399F,  -0.0427706F, 0.9421031F,
};

std::uint16_t float_to_half(float value) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    const auto half = static_cast<_Float16>(value);
    std::uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

float half_to_float(std::uint16_t bits) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    _Float16 half = 0.0F;
    std::memcpy(&half, &bits, sizeof(bits));
    return static_cast<float>(half);
}

float half_roundtrip(float value) {
    return half_to_float(float_to_half(value));
}

std::array<float, 9> mul3(const std::array<float, 9>& lhs, const std::array<float, 9>& rhs) {
    return {
        lhs[0] * rhs[0] + lhs[1] * rhs[3] + lhs[2] * rhs[6],
        lhs[0] * rhs[1] + lhs[1] * rhs[4] + lhs[2] * rhs[7],
        lhs[0] * rhs[2] + lhs[1] * rhs[5] + lhs[2] * rhs[8],
        lhs[3] * rhs[0] + lhs[4] * rhs[3] + lhs[5] * rhs[6],
        lhs[3] * rhs[1] + lhs[4] * rhs[4] + lhs[5] * rhs[7],
        lhs[3] * rhs[2] + lhs[4] * rhs[5] + lhs[5] * rhs[8],
        lhs[6] * rhs[0] + lhs[7] * rhs[3] + lhs[8] * rhs[6],
        lhs[6] * rhs[1] + lhs[7] * rhs[4] + lhs[8] * rhs[7],
        lhs[6] * rhs[2] + lhs[7] * rhs[5] + lhs[8] * rhs[8],
    };
}

std::array<float, 3> mul3(const std::array<float, 9>& matrix, const std::array<float, 3>& value) {
    return {matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
            matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
            matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]};
}

std::array<float, 9> inverse3(const std::array<float, 9>& m) {
    const auto det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    const auto inv_det = 1.0F / det;
    return std::array<float, 9>{
        (m[4] * m[8] - m[5] * m[7]) * inv_det, (m[2] * m[7] - m[1] * m[8]) * inv_det,
        (m[1] * m[5] - m[2] * m[4]) * inv_det, (m[5] * m[6] - m[3] * m[8]) * inv_det,
        (m[0] * m[8] - m[2] * m[6]) * inv_det, (m[2] * m[3] - m[0] * m[5]) * inv_det,
        (m[3] * m[7] - m[4] * m[6]) * inv_det, (m[1] * m[6] - m[0] * m[7]) * inv_det,
        (m[0] * m[4] - m[1] * m[3]) * inv_det,
    };
}

float bayer_sample(const Image& image, int x, int y) {
    x = std::clamp(x, 0, image.width - 1);
    y = std::clamp(y, 0, image.height - 1);
    return image.pixels[static_cast<std::size_t>(y) * image.width + x];
}

std::array<float, 4> demosaic_rgba(const Image& bayer, int x, int y) {
    const auto center = bayer_sample(bayer, x, y);
    const auto horizontal = 0.5F * (bayer_sample(bayer, x - 1, y) + bayer_sample(bayer, x + 1, y));
    const auto vertical = 0.5F * (bayer_sample(bayer, x, y - 1) + bayer_sample(bayer, x, y + 1));
    const auto cross = 0.25F * (bayer_sample(bayer, x - 1, y) + bayer_sample(bayer, x + 1, y) +
                                bayer_sample(bayer, x, y - 1) + bayer_sample(bayer, x, y + 1));
    const auto diagonal =
        0.25F * (bayer_sample(bayer, x - 1, y - 1) + bayer_sample(bayer, x + 1, y - 1) +
                 bayer_sample(bayer, x - 1, y + 1) + bayer_sample(bayer, x + 1, y + 1));
    const auto cfa = ((y & 1) * 2) + (x & 1);
    if (cfa == 0) {
        return {center, cross, diagonal, 1.0F};
    }
    if (cfa == 3) {
        return {diagonal, cross, center, 1.0F};
    }
    if ((y & 1) == 0) {
        return {horizontal, center, vertical, 1.0F};
    }
    return {vertical, center, horizontal, 1.0F};
}

Image linearize_input() {
    Image image{8, 4, 1, {}};
    image.pixels.reserve(32);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(static_cast<float>(((y * image.width) + x) % 16));
        }
    }
    return image;
}

Image linearize_output(const Image& input) {
    constexpr std::array<float, 16> table{0.0F,   11.0F,  23.0F,  36.0F,  50.0F,  65.0F,
                                          81.0F,  98.0F,  116.0F, 135.0F, 155.0F, 176.0F,
                                          198.0F, 221.0F, 245.0F, 270.0F};
    Image image{input.width, input.height, 1, {}};
    image.pixels.reserve(input.pixels.size());
    for (const auto value : input.pixels) {
        image.pixels.push_back(table[static_cast<std::size_t>(value)]);
    }
    return image;
}

Image blacklevel_input() {
    Image image{8, 4, 1, {}};
    image.pixels.reserve(32);
    constexpr std::array<int, 4> pattern{0, 1, 1, 2};
    constexpr std::array<float, 4> black{64.0F, 96.0F, 128.0F, 128.0F};
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto cfa = ((y & 1) * 2) + (x & 1);
            const auto channel = pattern[static_cast<std::size_t>(cfa)];
            image.pixels.push_back(black[static_cast<std::size_t>(channel)] +
                                   static_cast<float>((x + y + 1) * 42));
        }
    }
    return image;
}

Image blacklevel_output(const Image& input) {
    Image image{input.width, input.height, 1, {}};
    image.pixels.reserve(input.pixels.size());
    constexpr std::array<int, 4> pattern{0, 1, 1, 2};
    constexpr std::array<float, 4> black{64.0F, 96.0F, 128.0F, 128.0F};
    constexpr float white = 1024.0F;
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto cfa = ((y & 1) * 2) + (x & 1);
            const auto channel = pattern[static_cast<std::size_t>(cfa)];
            const auto offset = static_cast<std::size_t>(y) * input.width + x;
            const auto scaled = (input.pixels[offset] - black[static_cast<std::size_t>(channel)]) /
                                (white - black[static_cast<std::size_t>(channel)]);
            image.pixels.push_back(std::clamp(scaled, 0.0F, 1.0F));
        }
    }
    return image;
}

Image demosaic_input() {
    Image image{8, 8, 1, {}};
    image.pixels.reserve(64);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.05F + (0.07F * static_cast<float>(x)) +
                                   (0.035F * static_cast<float>(y)));
        }
    }
    return image;
}

Image demosaic_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size() * 4U);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto rgba = demosaic_rgba(input, x, y);
            for (const auto value : rgba) {
                image.pixels.push_back(half_roundtrip(value));
            }
        }
    }
    return image;
}

Image wb_input() {
    Image image{4, 4, 4, {}};
    image.pixels.reserve(64);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.05F + (0.015F * static_cast<float>(x)));
            image.pixels.push_back(0.12F + (0.01F * static_cast<float>(y)));
            image.pixels.push_back(0.02F + (0.006F * static_cast<float>(x + y)));
            image.pixels.push_back(1.0F);
        }
    }
    return image;
}

Image wb_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    constexpr std::array<float, 3> neutral{0.5F, 1.0F, 0.25F};
    for (std::size_t i = 0; i < input.pixels.size(); i += 4U) {
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 0U]) / neutral[0]));
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 1U]) / neutral[1]));
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 2U]) / neutral[2]));
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 3U])));
    }
    return image;
}

Image colormatrix_input() {
    Image image{4, 4, 4, {}};
    image.pixels.reserve(64);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.08F + (0.01F * static_cast<float>(x)));
            image.pixels.push_back(0.09F + (0.012F * static_cast<float>(y)));
            image.pixels.push_back(0.04F + (0.006F * static_cast<float>(x + y)));
            image.pixels.push_back(1.0F);
        }
    }
    return image;
}

Image colormatrix_output(const Image& input) {
    constexpr std::array<float, 9> color_matrix1{2.0F, 0.0F, 0.0F, 0.0F, 4.0F,
                                                 0.0F, 0.0F, 0.0F, 0.5F};
    const auto camera_to_xyz_d50 = inverse3(color_matrix1);
    const auto transform = mul3(kXyzD65ToRec2020, mul3(kD50ToD65, camera_to_xyz_d50));

    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (std::size_t i = 0; i < input.pixels.size(); i += 4U) {
        const std::array<float, 3> rgb{half_roundtrip(input.pixels[i + 0U]),
                                       half_roundtrip(input.pixels[i + 1U]),
                                       half_roundtrip(input.pixels[i + 2U])};
        const auto mapped = mul3(transform, rgb);
        image.pixels.push_back(half_roundtrip(mapped[0]));
        image.pixels.push_back(half_roundtrip(mapped[1]));
        image.pixels.push_back(half_roundtrip(mapped[2]));
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 3U])));
    }
    return image;
}

bool write_image(const std::filesystem::path& path, const Image& image) {
    std::filesystem::create_directories(path.parent_path());
    OIIO::ImageSpec spec{image.width, image.height, image.channels, OIIO::TypeDesc::FLOAT};
    OIIO::ImageBuf buffer{path.string(), spec};
    const OIIO::ROI roi{0, image.width, 0, image.height, 0, 1, 0, image.channels};
    if (!buffer.set_pixels(roi, OIIO::TypeDesc::FLOAT, image.pixels.data())) {
        std::cerr << "failed to set pixels for " << path << ": " << buffer.geterror() << '\n';
        return false;
    }
    if (!buffer.write(path.string(), OIIO::TypeDesc::FLOAT)) {
        std::cerr << "failed to write " << path << ": " << buffer.geterror() << '\n';
        return false;
    }
    return true;
}

bool write_pair(const std::filesystem::path& root, const std::string& node, const Image& input,
                const Image& output) {
    return write_image(root / node / "in.exr", input) &&
           write_image(root / node / "out.exr", output);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gen_golden_isp_nodes <tests/golden output dir>\n";
        return 2;
    }

    const std::filesystem::path root{argv[1]};
    const auto lin_in = linearize_input();
    const auto black_in = blacklevel_input();
    const auto demosaic_in = demosaic_input();
    const auto wb_in = wb_input();
    const auto cm_in = colormatrix_input();

    const bool ok =
        write_pair(root, "linearize.dng_lut", lin_in, linearize_output(lin_in)) &&
        write_pair(root, "blacklevel.dng_levels", black_in, blacklevel_output(black_in)) &&
        write_pair(root, "demosaic.bilinear", demosaic_in, demosaic_output(demosaic_in)) &&
        write_pair(root, "wb.dual_illuminant", wb_in, wb_output(wb_in)) &&
        write_pair(root, "colormatrix.dng_to_working", cm_in, colormatrix_output(cm_in));
    return ok ? 0 : 1;
}
