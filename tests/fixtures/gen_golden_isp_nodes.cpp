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

float bayer_sample(const Image& image, int x, int y) {
    x = std::clamp(x, 0, image.width - 1);
    y = std::clamp(y, 0, image.height - 1);
    return image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) +
                        static_cast<std::size_t>(x)];
}

int cfa_at(int x, int y) {
    constexpr std::array<int, 4> pattern{0, 1, 1, 2};
    return pattern[static_cast<std::size_t>(((y & 1) * 2) + (x & 1))];
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
    const auto cfa = cfa_at(x, y);
    if (cfa == 0) {
        return {center, cross, diagonal, 1.0F};
    }
    if (cfa == 2) {
        return {diagonal, cross, center, 1.0F};
    }
    if (cfa_at(x - 1, y) == 0) {
        return {horizontal, center, vertical, 1.0F};
    }
    return {vertical, center, horizontal, 1.0F};
}

float rcd_green(const Image& bayer, int x, int y) {
    const auto center = bayer_sample(bayer, x, y);
    if (cfa_at(x, y) == 1) {
        return center;
    }
    const auto gh = 0.5F * (bayer_sample(bayer, x - 1, y) + bayer_sample(bayer, x + 1, y));
    const auto gv = 0.5F * (bayer_sample(bayer, x, y - 1) + bayer_sample(bayer, x, y + 1));
    const auto dh = std::abs(bayer_sample(bayer, x - 2, y) - bayer_sample(bayer, x + 2, y));
    const auto dv = std::abs(bayer_sample(bayer, x, y - 2) - bayer_sample(bayer, x, y + 2));
    if (dh < dv) {
        return gh;
    }
    if (dv < dh) {
        return gv;
    }
    return 0.5F * (gh + gv);
}

float rcd_ratio(const Image& bayer, int x, int y) {
    return bayer_sample(bayer, x, y) / std::max(rcd_green(bayer, x, y), 0.000001F);
}

float rcd_horizontal(const Image& bayer, int x, int y) {
    return rcd_green(bayer, x, y) * 0.5F *
           (rcd_ratio(bayer, x - 1, y) + rcd_ratio(bayer, x + 1, y));
}

float rcd_vertical(const Image& bayer, int x, int y) {
    return rcd_green(bayer, x, y) * 0.5F *
           (rcd_ratio(bayer, x, y - 1) + rcd_ratio(bayer, x, y + 1));
}

float rcd_diagonal(const Image& bayer, int x, int y) {
    return rcd_green(bayer, x, y) * 0.25F *
           (rcd_ratio(bayer, x - 1, y - 1) + rcd_ratio(bayer, x + 1, y - 1) +
            rcd_ratio(bayer, x - 1, y + 1) + rcd_ratio(bayer, x + 1, y + 1));
}

std::array<float, 4> demosaic_rcd_rgba(const Image& bayer, int x, int y) {
    const auto center = bayer_sample(bayer, x, y);
    const auto cfa = cfa_at(x, y);
    const auto horizontal_red = cfa_at(x - 1, y) == 0;
    const auto horizontal_blue = cfa_at(x - 1, y) == 2;
    const auto red = std::max(0.0F, cfa == 0         ? center
                                    : cfa == 2       ? rcd_diagonal(bayer, x, y)
                                    : horizontal_red ? rcd_horizontal(bayer, x, y)
                                                     : rcd_vertical(bayer, x, y));
    const auto green = std::max(0.0F, rcd_green(bayer, x, y));
    const auto blue = std::max(0.0F, cfa == 2          ? center
                                     : cfa == 0        ? rcd_diagonal(bayer, x, y)
                                     : horizontal_blue ? rcd_horizontal(bayer, x, y)
                                                       : rcd_vertical(bayer, x, y));
    return {red, green, blue, 1.0F};
}

float amaze_green(const Image& bayer, int x, int y) {
    const auto center = bayer_sample(bayer, x, y);
    if (cfa_at(x, y) == 1) {
        return center;
    }

    const auto gh =
        0.5F * (bayer_sample(bayer, x - 1, y) + bayer_sample(bayer, x + 1, y)) +
        0.25F * ((2.0F * center) - bayer_sample(bayer, x - 2, y) - bayer_sample(bayer, x + 2, y));
    const auto gv =
        0.5F * (bayer_sample(bayer, x, y - 1) + bayer_sample(bayer, x, y + 1)) +
        0.25F * ((2.0F * center) - bayer_sample(bayer, x, y - 2) - bayer_sample(bayer, x, y + 2));
    const auto grad_h = std::abs(bayer_sample(bayer, x - 1, y) - bayer_sample(bayer, x + 1, y)) +
                        std::abs(bayer_sample(bayer, x - 2, y) - center) +
                        std::abs(bayer_sample(bayer, x + 2, y) - center);
    const auto grad_v = std::abs(bayer_sample(bayer, x, y - 1) - bayer_sample(bayer, x, y + 1)) +
                        std::abs(bayer_sample(bayer, x, y - 2) - center) +
                        std::abs(bayer_sample(bayer, x, y + 2) - center);
    if (grad_h < grad_v) {
        return gh;
    }
    if (grad_v < grad_h) {
        return gv;
    }
    return 0.5F * (gh + gv);
}

float amaze_chroma_horizontal(const Image& bayer, int x, int y) {
    return 0.5F * ((bayer_sample(bayer, x - 1, y) - amaze_green(bayer, x - 1, y)) +
                   (bayer_sample(bayer, x + 1, y) - amaze_green(bayer, x + 1, y)));
}

float amaze_chroma_vertical(const Image& bayer, int x, int y) {
    return 0.5F * ((bayer_sample(bayer, x, y - 1) - amaze_green(bayer, x, y - 1)) +
                   (bayer_sample(bayer, x, y + 1) - amaze_green(bayer, x, y + 1)));
}

float amaze_chroma_diagonal(const Image& bayer, int x, int y) {
    return 0.25F * ((bayer_sample(bayer, x - 1, y - 1) - amaze_green(bayer, x - 1, y - 1)) +
                    (bayer_sample(bayer, x + 1, y - 1) - amaze_green(bayer, x + 1, y - 1)) +
                    (bayer_sample(bayer, x - 1, y + 1) - amaze_green(bayer, x - 1, y + 1)) +
                    (bayer_sample(bayer, x + 1, y + 1) - amaze_green(bayer, x + 1, y + 1)));
}

std::array<float, 4> demosaic_amaze_rgba(const Image& bayer, int x, int y) {
    const auto center = bayer_sample(bayer, x, y);
    const auto green = amaze_green(bayer, x, y);
    const auto cfa = cfa_at(x, y);
    const auto horizontal_red = cfa_at(x - 1, y) == 0;
    const auto horizontal_blue = cfa_at(x - 1, y) == 2;
    const auto red = std::max(0.0F, cfa == 0         ? center
                                    : cfa == 2       ? green + amaze_chroma_diagonal(bayer, x, y)
                                    : horizontal_red ? green + amaze_chroma_horizontal(bayer, x, y)
                                                     : green + amaze_chroma_vertical(bayer, x, y));
    const auto blue =
        std::max(0.0F, cfa == 2          ? center
                       : cfa == 0        ? green + amaze_chroma_diagonal(bayer, x, y)
                       : horizontal_blue ? green + amaze_chroma_horizontal(bayer, x, y)
                                         : green + amaze_chroma_vertical(bayer, x, y));
    return {red, std::max(0.0F, green), blue, 1.0F};
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
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(input.width)) +
                static_cast<std::size_t>(x);
            const auto scaled = (input.pixels[offset] - black[static_cast<std::size_t>(channel)]) /
                                (white - black[static_cast<std::size_t>(channel)]);
            image.pixels.push_back(std::clamp(scaled, 0.0F, 1.0F));
        }
    }
    return image;
}

Image gainmap_input() {
    Image image{8, 8, 1, {}};
    image.pixels.reserve(64);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.12F + (0.018F * static_cast<float>(x)) +
                                   (0.013F * static_cast<float>(y)));
        }
    }
    return image;
}

Image gainmap_output(const Image& input) {
    Image image{input.width, input.height, 1, {}};
    image.pixels.reserve(input.pixels.size());
    constexpr std::array<float, 4> gains{1.0F, 1.2F, 1.4F, 1.8F};
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto tx = static_cast<float>(x) / static_cast<float>(input.width - 1);
            const auto ty = static_cast<float>(y) / static_cast<float>(input.height - 1);
            const auto top = gains[0] + ((gains[1] - gains[0]) * tx);
            const auto bottom = gains[2] + ((gains[3] - gains[2]) * tx);
            const auto gain = top + ((bottom - top) * ty);
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(input.width)) +
                static_cast<std::size_t>(x);
            image.pixels.push_back(input.pixels[offset] * gain);
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

Image demosaic_rcd_input() {
    Image image{16, 16, 1, {}};
    image.pixels.reserve(256);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.04F + (0.026F * static_cast<float>(x)) +
                                   (0.019F * static_cast<float>(y)) +
                                   (0.003F * static_cast<float>((x * y) % 5)));
        }
    }
    return image;
}

Image demosaic_amaze_input() {
    Image image{16, 16, 1, {}};
    image.pixels.reserve(256);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.035F + (0.021F * static_cast<float>(x)) +
                                   (0.017F * static_cast<float>(y)) +
                                   (0.004F * static_cast<float>((x * 3 + y * 5) % 7)));
        }
    }
    return image;
}

Image demosaic_rcd_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size() * 4U);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto rgba = demosaic_rcd_rgba(input, x, y);
            for (const auto value : rgba) {
                image.pixels.push_back(half_roundtrip(value));
            }
        }
    }
    return image;
}

Image demosaic_amaze_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size() * 4U);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto rgba = demosaic_amaze_rgba(input, x, y);
            for (const auto value : rgba) {
                image.pixels.push_back(half_roundtrip(value));
            }
        }
    }
    return image;
}

Image quad_bayer_remosaic_input() {
    Image image{8, 8, 1, {}};
    image.pixels.reserve(64);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(static_cast<float>(100 + (y * 23) + (x * 11)));
        }
    }
    return image;
}

int qbc_color(int x, int y) {
    const auto mx = ((x % 4) + 4) % 4;
    const auto my = ((y % 4) + 4) % 4;
    if (my < 2 && mx < 2) {
        return 0;
    }
    if (my >= 2 && mx >= 2) {
        return 2;
    }
    return 1;
}

int rggb_color(int x, int y) {
    if ((y & 1) == 0 && (x & 1) == 0) {
        return 0;
    }
    if ((y & 1) != 0 && (x & 1) != 0) {
        return 2;
    }
    return 1;
}

float qbc_remosaic_sample(const Image& input, int x, int y) {
    const auto target = rggb_color(x, y);
    std::uint32_t weighted_sum = 0;
    std::uint32_t weight_sum = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const auto sx = std::clamp(x + dx, 0, input.width - 1);
            const auto sy = std::clamp(y + dy, 0, input.height - 1);
            if (qbc_color(sx, sy) != target) {
                continue;
            }
            const auto grad_x = std::abs(static_cast<int>(bayer_sample(input, sx - 1, sy)) -
                                         static_cast<int>(bayer_sample(input, sx + 1, sy)));
            const auto grad_y = std::abs(static_cast<int>(bayer_sample(input, sx, sy - 1)) -
                                         static_cast<int>(bayer_sample(input, sx, sy + 1)));
            const auto penalty = 64 + grad_x + grad_y + ((std::abs(dx) + std::abs(dy)) * 16);
            const auto weight = static_cast<std::uint32_t>(std::max(1, 65536 / penalty));
            weighted_sum += weight * static_cast<std::uint32_t>(bayer_sample(input, sx, sy));
            weight_sum += weight;
        }
    }
    return static_cast<float>((weighted_sum + (weight_sum / 2U)) / weight_sum);
}

Image quad_bayer_remosaic_output(const Image& input) {
    Image image{input.width, input.height, 1, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            image.pixels.push_back(qbc_remosaic_sample(input, x, y));
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

std::array<float, 3> greyworld_neutral(const Image& input) {
    std::array<double, 3> sums{};
    for (std::size_t i = 0; i < input.pixels.size(); i += 4U) {
        sums[0] += half_roundtrip(input.pixels[i + 0U]);
        sums[1] += half_roundtrip(input.pixels[i + 1U]);
        sums[2] += half_roundtrip(input.pixels[i + 2U]);
    }
    return {static_cast<float>(sums[0] / sums[1]), 1.0F, static_cast<float>(sums[2] / sums[1])};
}

Image greyworld_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    const auto neutral = greyworld_neutral(input);
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
    constexpr std::array<float, 9> camera_to_xyz_d50{0.5F, 0.0F, 0.0F, 0.0F, 0.25F,
                                                     0.0F, 0.0F, 0.0F, 2.0F};
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

float denoise_sample(const Image& input, int x, int y, int c) {
    x = std::clamp(x, 0, input.width - 1);
    y = std::clamp(y, 0, input.height - 1);
    const auto offset = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(input.width)) +
                         static_cast<std::size_t>(x)) *
                            4U +
                        static_cast<std::size_t>(c);
    return half_roundtrip(input.pixels[offset]);
}

float guided_mean3x3(const Image& input, int x, int y, int c) {
    auto sum = 0.0F;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sum += denoise_sample(input, x + dx, y + dy, c);
        }
    }
    return sum / 9.0F;
}

float guided_corr3x3(const Image& input, int x, int y, int c) {
    auto sum = 0.0F;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto value = denoise_sample(input, x + dx, y + dy, c);
            sum += value * value;
        }
    }
    return sum / 9.0F;
}

float guided_a(const Image& input, int x, int y, int c) {
    constexpr float epsilon = 0.015F;
    const auto mean = guided_mean3x3(input, x, y, c);
    const auto variance = std::max(0.0F, guided_corr3x3(input, x, y, c) - (mean * mean));
    return variance / (variance + epsilon);
}

float guided_b(const Image& input, int x, int y, int c) {
    const auto mean = guided_mean3x3(input, x, y, c);
    return mean - (guided_a(input, x, y, c) * mean);
}

float guided_mean_a(const Image& input, int x, int y, int c) {
    auto sum = 0.0F;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sum += guided_a(input, x + dx, y + dy, c);
        }
    }
    return sum / 9.0F;
}

float guided_mean_b(const Image& input, int x, int y, int c) {
    auto sum = 0.0F;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sum += guided_b(input, x + dx, y + dy, c);
        }
    }
    return sum / 9.0F;
}

Image denoise_input() {
    Image image{8, 8, 4, {}};
    image.pixels.reserve(256);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto base =
                0.08F + (0.018F * static_cast<float>(x)) + (0.014F * static_cast<float>(y));
            const auto chroma_noise = ((x + y) & 1) == 0 ? 0.035F : -0.035F;
            const auto luma_noise = static_cast<float>(((x * 3 + y * 5) % 5) - 2) * 0.006F;
            image.pixels.push_back(base + luma_noise + chroma_noise);
            image.pixels.push_back(base + (0.5F * luma_noise));
            image.pixels.push_back(base + luma_noise - chroma_noise);
            image.pixels.push_back(1.0F);
        }
    }
    return image;
}

Image guided_filter_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                if (c == 3) {
                    image.pixels.push_back(half_roundtrip(denoise_sample(input, x, y, c)));
                    continue;
                }
                const auto filtered =
                    (guided_mean_a(input, x, y, c) * denoise_sample(input, x, y, c)) +
                    guided_mean_b(input, x, y, c);
                image.pixels.push_back(half_roundtrip(std::max(0.0F, filtered)));
            }
        }
    }
    return image;
}

std::array<float, 3> ycocg_at(const Image& input, int x, int y) {
    const auto r = denoise_sample(input, x, y, 0);
    const auto g = denoise_sample(input, x, y, 1);
    const auto b = denoise_sample(input, x, y, 2);
    return {(r + (2.0F * g) + b) * 0.25F, r - b, g - ((r + b) * 0.5F)};
}

float soft_threshold(float value) {
    constexpr float threshold = 0.035F;
    if (value > threshold) {
        return value - threshold;
    }
    if (value < -threshold) {
        return value + threshold;
    }
    return 0.0F;
}

float wavelet_chroma(const Image& input, int x, int y, int component) {
    const auto bx = (x / 2) * 2;
    const auto by = (y / 2) * 2;
    const auto p00 = ycocg_at(input, bx, by)[static_cast<std::size_t>(component)];
    const auto p10 = ycocg_at(input, bx + 1, by)[static_cast<std::size_t>(component)];
    const auto p01 = ycocg_at(input, bx, by + 1)[static_cast<std::size_t>(component)];
    const auto p11 = ycocg_at(input, bx + 1, by + 1)[static_cast<std::size_t>(component)];
    const auto ll = 0.25F * (p00 + p10 + p01 + p11);
    const auto h = soft_threshold(0.25F * (p00 - p10 + p01 - p11));
    const auto v = soft_threshold(0.25F * (p00 + p10 - p01 - p11));
    const auto d = soft_threshold(0.25F * (p00 - p10 - p01 + p11));

    if ((x & 1) == 0 && (y & 1) == 0) {
        return ll + h + v + d;
    }
    if ((x & 1) != 0 && (y & 1) == 0) {
        return ll - h + v - d;
    }
    if ((x & 1) == 0) {
        return ll + h - v - d;
    }
    return ll - h - v + d;
}

Image wavelet_bayes_shrink_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto ycc = ycocg_at(input, x, y);
            const auto co = wavelet_chroma(input, x, y, 1);
            const auto cg = wavelet_chroma(input, x, y, 2);
            const auto t = ycc[0] - (0.5F * cg);
            image.pixels.push_back(half_roundtrip(std::max(0.0F, t + (0.5F * co))));
            image.pixels.push_back(half_roundtrip(std::max(0.0F, t + cg)));
            image.pixels.push_back(half_roundtrip(std::max(0.0F, t - (0.5F * co))));
            image.pixels.push_back(half_roundtrip(denoise_sample(input, x, y, 3)));
        }
    }
    return image;
}

float bm3d_mean2x2(const Image& input, int x, int y, int c) {
    const auto bx = (x / 2) * 2;
    const auto by = (y / 2) * 2;
    return 0.25F *
           (denoise_sample(input, bx, by, c) + denoise_sample(input, bx + 1, by, c) +
            denoise_sample(input, bx, by + 1, c) + denoise_sample(input, bx + 1, by + 1, c));
}

Image bm3d_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                if (c == 3) {
                    image.pixels.push_back(half_roundtrip(denoise_sample(input, x, y, c)));
                    continue;
                }
                image.pixels.push_back(
                    half_roundtrip(std::max(0.0F, bm3d_mean2x2(input, x, y, c))));
            }
        }
    }
    return image;
}

Image sharpen_input() {
    Image image{8, 8, 4, {}};
    image.pixels.reserve(256);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto value = x < 4 ? 0.24F + (0.004F * static_cast<float>(y))
                                     : 0.66F + (0.004F * static_cast<float>(y));
            image.pixels.push_back(value);
            image.pixels.push_back(value);
            image.pixels.push_back(value);
            image.pixels.push_back(1.0F);
        }
    }
    return image;
}

float sharpen_mean3x3(const Image& input, int x, int y, int c) {
    auto sum = 0.0F;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sum += denoise_sample(input, x + dx, y + dy, c);
        }
    }
    return sum / 9.0F;
}

Image sharpen_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                const auto value = denoise_sample(input, x, y, c);
                if (c == 3) {
                    image.pixels.push_back(half_roundtrip(value));
                    continue;
                }
                const auto blur = sharpen_mean3x3(input, x, y, c);
                image.pixels.push_back(
                    half_roundtrip(std::max(0.0F, value + (0.75F * (value - blur)))));
            }
        }
    }
    return image;
}

Image tone_input() {
    Image image{8, 4, 4, {}};
    image.pixels.reserve(128);
    const auto count = image.width * image.height;
    for (int i = 0; i < count; ++i) {
        const auto t = static_cast<float>(i) / static_cast<float>(count - 1);
        const auto value = 16.0F * t * t;
        image.pixels.push_back(value);
        image.pixels.push_back(0.65F * value);
        image.pixels.push_back(0.35F * value);
        image.pixels.push_back(1.0F);
    }
    return image;
}

float aces_fit(float value) {
    value = std::max(0.0F, value);
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    return std::clamp((value * ((a * value) + b)) / ((value * ((c * value) + d)) + e), 0.0F, 1.0F);
}

float filmic_curve(float value) {
    value = std::max(0.0F, value);
    constexpr float contrast = 6.0F;
    constexpr float white = 18.0F;
    return std::clamp(std::log(1.0F + (contrast * value)) / std::log(1.0F + (contrast * white)),
                      0.0F, 1.0F);
}

float reinhard(float value) {
    value = std::max(0.0F, value);
    return value / (1.0F + value);
}

using ToneCurve = float (*)(float);

Image tone_output(const Image& input, ToneCurve curve) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (std::size_t i = 0; i < input.pixels.size(); i += 4U) {
        image.pixels.push_back(half_roundtrip(curve(half_roundtrip(input.pixels[i + 0U]))));
        image.pixels.push_back(half_roundtrip(curve(half_roundtrip(input.pixels[i + 1U]))));
        image.pixels.push_back(half_roundtrip(curve(half_roundtrip(input.pixels[i + 2U]))));
        image.pixels.push_back(half_roundtrip(half_roundtrip(input.pixels[i + 3U])));
    }
    return image;
}

Image opcode_list_3_input() {
    Image image{8, 8, 4, {}};
    image.pixels.reserve(256);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.push_back(0.10F + (0.012F * static_cast<float>(x)));
            image.pixels.push_back(0.08F + (0.010F * static_cast<float>(y)));
            image.pixels.push_back(0.06F + (0.006F * static_cast<float>(x + y)));
            image.pixels.push_back(1.0F);
        }
    }
    return image;
}

float normalized_radius2(int x, int y, int width, int height) {
    const auto cx = 0.5F * static_cast<float>(width - 1);
    const auto cy = 0.5F * static_cast<float>(height - 1);
    const auto dx0 = cx;
    const auto dx1 = static_cast<float>(width - 1) - cx;
    const auto dy0 = cy;
    const auto dy1 = static_cast<float>(height - 1) - cy;
    const auto max_distance2 = std::max({(dx0 * dx0) + (dy0 * dy0), (dx1 * dx1) + (dy0 * dy0),
                                         (dx0 * dx0) + (dy1 * dy1), (dx1 * dx1) + (dy1 * dy1)});
    const auto dx = static_cast<float>(x) - cx;
    const auto dy = static_cast<float>(y) - cy;
    return ((dx * dx) + (dy * dy)) / max_distance2;
}

Image opcode_list_3_output(const Image& input) {
    Image image{input.width, input.height, 4, {}};
    image.pixels.reserve(input.pixels.size());
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto gain = 1.0F + (0.5F * normalized_radius2(x, y, input.width, input.height));
            const auto offset =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(input.width)) +
                 static_cast<std::size_t>(x)) *
                4U;
            image.pixels.push_back(
                half_roundtrip(half_roundtrip(input.pixels[offset + 0U]) * gain));
            image.pixels.push_back(
                half_roundtrip(half_roundtrip(input.pixels[offset + 1U]) * gain));
            image.pixels.push_back(
                half_roundtrip(half_roundtrip(input.pixels[offset + 2U]) * gain));
            image.pixels.push_back(half_roundtrip(input.pixels[offset + 3U]));
        }
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
    const auto gainmap_in = gainmap_input();
    const auto demosaic_in = demosaic_input();
    const auto rcd_in = demosaic_rcd_input();
    const auto amaze_in = demosaic_amaze_input();
    const auto qbc_in = quad_bayer_remosaic_input();
    const auto opcode3_in = opcode_list_3_input();
    const auto wb_in = wb_input();
    const auto cm_in = colormatrix_input();
    const auto denoise_in = denoise_input();
    const auto sharpen_in = sharpen_input();
    const auto tone_in = tone_input();

    const bool ok =
        write_pair(root, "linearize.dng_lut", lin_in, linearize_output(lin_in)) &&
        write_pair(root, "blacklevel.dng_levels", black_in, blacklevel_output(black_in)) &&
        write_pair(root, "lens.shading_gainmap", gainmap_in, gainmap_output(gainmap_in)) &&
        write_pair(root, "demosaic.bilinear", demosaic_in, demosaic_output(demosaic_in)) &&
        write_pair(root, "demosaic.rcd", rcd_in, demosaic_rcd_output(rcd_in)) &&
        write_pair(root, "demosaic.amaze", amaze_in, demosaic_amaze_output(amaze_in)) &&
        write_pair(root, "demosaic.quad_bayer_remosaic", qbc_in,
                   quad_bayer_remosaic_output(qbc_in)) &&
        write_pair(root, "lens.dng_opcode_list_3", opcode3_in, opcode_list_3_output(opcode3_in)) &&
        write_pair(root, "wb.dual_illuminant", wb_in, wb_output(wb_in)) &&
        write_pair(root, "wb.greyworld_auto", wb_in, greyworld_output(wb_in)) &&
        write_pair(root, "colormatrix.dng_to_working", cm_in, colormatrix_output(cm_in)) &&
        write_pair(root, "denoise.bm3d", denoise_in, bm3d_output(denoise_in)) &&
        write_pair(root, "denoise.guided_filter", denoise_in, guided_filter_output(denoise_in)) &&
        write_pair(root, "denoise.wavelet_bayes_shrink", denoise_in,
                   wavelet_bayes_shrink_output(denoise_in)) &&
        write_pair(root, "tone.aces_filmic", tone_in, tone_output(tone_in, aces_fit)) &&
        write_pair(root, "tone.filmic_rgb", tone_in, tone_output(tone_in, filmic_curve)) &&
        write_pair(root, "tone.reinhard", tone_in, tone_output(tone_in, reinhard)) &&
        write_pair(root, "sharpen.edge_aware_usm", sharpen_in, sharpen_output(sharpen_in));
    return ok ? 0 : 1;
}
