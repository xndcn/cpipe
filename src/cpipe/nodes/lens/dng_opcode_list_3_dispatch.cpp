// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <cmath>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "../detail/Float16.hpp"
#include "../detail/P1ParamDispatch.hpp"

namespace {

using cpipe::nodes::detail::Opcode3BadPoint;
using cpipe::nodes::detail::Opcode3BadRect;
using cpipe::nodes::detail::Opcode3DispatchHeader;
using cpipe::nodes::detail::Opcode3DispatchRecord;
using cpipe::nodes::detail::Opcode3WarpCoefficient;

constexpr std::uint32_t kOpcodeUnknown = 0;
constexpr std::uint32_t kOpcodeWarpRectilinear = 1;
constexpr std::uint32_t kOpcodeFixVignetteRadial = 3;
constexpr std::uint32_t kOpcodeFixBadPixelsConstant = 4;
constexpr std::uint32_t kOpcodeFixBadPixelsList = 5;
constexpr std::uint32_t kOpcodeTrimBounds = 6;
constexpr int kChannels = 4;

std::size_t pixel_offset(int width, int x, int y, int c) {
    return ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
            static_cast<std::size_t>(x)) *
               kChannels +
           static_cast<std::size_t>(c);
}

float sample_pixel(const std::vector<std::uint16_t>& image, int width, int height, int x, int y,
                   int c) {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return cpipe::nodes::detail::half_to_float(image[pixel_offset(width, x, y, c)]);
}

float cubic(float p0, float p1, float p2, float p3, float t) {
    const auto a0 = (-0.5F * p0) + (1.5F * p1) - (1.5F * p2) + (0.5F * p3);
    const auto a1 = p0 - (2.5F * p1) + (2.0F * p2) - (0.5F * p3);
    const auto a2 = (-0.5F * p0) + (0.5F * p2);
    return (((a0 * t) + a1) * t + a2) * t + p1;
}

float sample_bicubic(const std::vector<std::uint16_t>& image, int width, int height, double x,
                     double y, int c) {
    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));
    const auto ix = static_cast<int>(std::floor(x));
    const auto iy = static_cast<int>(std::floor(y));
    const auto tx = static_cast<float>(x - static_cast<double>(ix));
    const auto ty = static_cast<float>(y - static_cast<double>(iy));

    float rows[4]{};
    for (int row = 0; row < 4; ++row) {
        const auto sy = iy + row - 1;
        rows[row] = cubic(sample_pixel(image, width, height, ix - 1, sy, c),
                          sample_pixel(image, width, height, ix, sy, c),
                          sample_pixel(image, width, height, ix + 1, sy, c),
                          sample_pixel(image, width, height, ix + 2, sy, c), tx);
    }
    return cubic(rows[0], rows[1], rows[2], rows[3], ty);
}

double max_corner_distance(double cx, double cy, int width, int height) {
    const auto x1 = static_cast<double>(width - 1);
    const auto y1 = static_cast<double>(height - 1);
    const auto d00 = (cx * cx) + (cy * cy);
    const auto d10 = ((x1 - cx) * (x1 - cx)) + (cy * cy);
    const auto d01 = (cx * cx) + ((y1 - cy) * (y1 - cy));
    const auto d11 = ((x1 - cx) * (x1 - cx)) + ((y1 - cy) * (y1 - cy));
    return std::sqrt(std::max({d00, d10, d01, d11}));
}

void copy_image(const std::vector<std::uint16_t>& src, std::vector<std::uint16_t>* dst) {
    *dst = src;
}

void apply_warp(const Opcode3DispatchRecord& record, const std::vector<std::uint16_t>& src,
                int width, int height, std::vector<std::uint16_t>* dst) {
    copy_image(src, dst);
    if (record.coefficient_count == 0U || width <= 1 || height <= 1) {
        return;
    }

    const auto cx = record.cx_hat * static_cast<double>(width - 1);
    const auto cy = record.cy_hat * static_cast<double>(height - 1);
    const auto m = max_corner_distance(cx, cy, width, height);
    if (m <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto dx = (static_cast<double>(x) - cx) / m;
            const auto dy = (static_cast<double>(y) - cy) / m;
            const auto r2 = (dx * dx) + (dy * dy);
            const auto r4 = r2 * r2;
            const auto r6 = r4 * r2;
            for (int c = 0; c < kChannels; ++c) {
                const auto coefficient_index = std::min<std::uint32_t>(
                    static_cast<std::uint32_t>(c), record.coefficient_count - 1U);
                const auto& k = record.coefficients[coefficient_index];
                const auto f = k.kr0 + (k.kr1 * r2) + (k.kr2 * r4) + (k.kr3 * r6);
                const auto tangential_x = (2.0 * k.kt0 * dx * dy) + (k.kt1 * (r2 + 2.0 * dx * dx));
                const auto tangential_y = (k.kt0 * (r2 + 2.0 * dy * dy)) + (2.0 * k.kt1 * dx * dy);
                const auto sx = cx + (m * ((f * dx) + tangential_x));
                const auto sy = cy + (m * ((f * dy) + tangential_y));
                (*dst)[pixel_offset(width, x, y, c)] = cpipe::nodes::detail::float_to_half(
                    sample_bicubic(src, width, height, sx, sy, c));
            }
        }
    }
}

void apply_vignette(const Opcode3DispatchRecord& record, const std::vector<std::uint16_t>& src,
                    int width, int height, std::vector<std::uint16_t>* dst) {
    copy_image(src, dst);
    if (width <= 1 || height <= 1) {
        return;
    }
    const auto cx = record.vignette_cx_hat * static_cast<double>(width - 1);
    const auto cy = record.vignette_cy_hat * static_cast<double>(height - 1);
    const auto m = max_corner_distance(cx, cy, width, height);
    if (m <= std::numeric_limits<double>::epsilon()) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto dx = (static_cast<double>(x) - cx) / m;
            const auto dy = (static_cast<double>(y) - cy) / m;
            const auto r2 = (dx * dx) + (dy * dy);
            const auto r4 = r2 * r2;
            const auto r6 = r4 * r2;
            const auto r8 = r4 * r4;
            const auto r10 = r8 * r2;
            const auto gain = 1.0 + (record.vignette_k[0] * r2) + (record.vignette_k[1] * r4) +
                              (record.vignette_k[2] * r6) + (record.vignette_k[3] * r8) +
                              (record.vignette_k[4] * r10);
            for (int c = 0; c < 3; ++c) {
                const auto offset = pixel_offset(width, x, y, c);
                const auto value = cpipe::nodes::detail::half_to_float(src[offset]);
                (*dst)[offset] = cpipe::nodes::detail::float_to_half(
                    static_cast<float>(static_cast<double>(value) * gain));
            }
        }
    }
}

float patched_channel(const std::vector<std::uint16_t>& src, int width, int height, int x, int y,
                      int c) {
    float sum = 0.0F;
    int count = 0;
    constexpr int kDx[4]{-1, 1, 0, 0};
    constexpr int kDy[4]{0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
        const auto nx = x + kDx[i];
        const auto ny = y + kDy[i];
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            continue;
        }
        sum += sample_pixel(src, width, height, nx, ny, c);
        ++count;
    }
    if (count == 0) {
        return sample_pixel(src, width, height, x, y, c);
    }
    return sum / static_cast<float>(count);
}

void patch_pixel(const std::vector<std::uint16_t>& src, int width, int height, int x, int y,
                 std::vector<std::uint16_t>* dst) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    for (int c = 0; c < 3; ++c) {
        (*dst)[pixel_offset(width, x, y, c)] =
            cpipe::nodes::detail::float_to_half(patched_channel(src, width, height, x, y, c));
    }
}

void apply_bad_pixels_constant(const Opcode3DispatchRecord& record,
                               const std::vector<std::uint16_t>& src, int width, int height,
                               std::vector<std::uint16_t>* dst) {
    copy_image(src, dst);
    const auto constant = static_cast<float>(record.constant);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool patch = false;
            for (int c = 0; c < 3; ++c) {
                const auto value = sample_pixel(src, width, height, x, y, c);
                patch = patch || std::abs(value - constant) <= 0.0001F;
            }
            if (patch) {
                patch_pixel(src, width, height, x, y, dst);
            }
        }
    }
}

void apply_bad_pixels_list(const Opcode3DispatchRecord& record,
                           const std::vector<std::uint16_t>& src, int width, int height,
                           const Opcode3BadPoint* points, const Opcode3BadRect* rects,
                           std::vector<std::uint16_t>* dst) {
    copy_image(src, dst);
    for (std::uint32_t i = 0; i < record.point_count; ++i) {
        const auto& point = points[static_cast<std::size_t>(record.point_offset) + i];
        patch_pixel(src, width, height, static_cast<int>(point.column), static_cast<int>(point.row),
                    dst);
    }
    for (std::uint32_t i = 0; i < record.rect_count; ++i) {
        const auto& rect = rects[static_cast<std::size_t>(record.rect_offset) + i];
        const auto top = std::min<int>(static_cast<int>(rect.top), height);
        const auto bottom = std::min<int>(static_cast<int>(rect.bottom), height);
        const auto left = std::min<int>(static_cast<int>(rect.left), width);
        const auto right = std::min<int>(static_cast<int>(rect.right), width);
        for (int y = std::max(0, top); y < std::max(0, bottom); ++y) {
            for (int x = std::max(0, left); x < std::max(0, right); ++x) {
                patch_pixel(src, width, height, x, y, dst);
            }
        }
    }
}

void apply_trim_bounds(const Opcode3DispatchRecord& record, const std::vector<std::uint16_t>& src,
                       int width, int height, std::vector<std::uint16_t>* dst) {
    copy_image(src, dst);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto inside = static_cast<std::uint32_t>(y) >= record.top &&
                                static_cast<std::uint32_t>(y) < record.bottom &&
                                static_cast<std::uint32_t>(x) >= record.left &&
                                static_cast<std::uint32_t>(x) < record.right;
            if (inside) {
                continue;
            }
            for (int c = 0; c < kChannels; ++c) {
                (*dst)[pixel_offset(width, x, y, c)] = cpipe::nodes::detail::float_to_half(0.0F);
            }
        }
    }
}

bool read_image(const halide_buffer_t* input, int width, int height,
                std::vector<std::uint16_t>* image) {
    if (input->dimensions != 3 || input->dim[2].extent != kChannels || input->host == nullptr) {
        return false;
    }
    const auto* in = reinterpret_cast<const std::uint16_t*>(input->host);
    image->resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kChannels);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < kChannels; ++c) {
                const auto input_offset = (y * input->dim[1].stride) + (x * input->dim[0].stride) +
                                          (c * input->dim[2].stride);
                (*image)[pixel_offset(width, x, y, c)] = in[input_offset];
            }
        }
    }
    return true;
}

bool write_image(const std::vector<std::uint16_t>& image, halide_buffer_t* output, int width,
                 int height) {
    if (output->dimensions != 3 || output->dim[2].extent != kChannels || output->host == nullptr ||
        output->dim[0].extent != width || output->dim[1].extent != height) {
        return false;
    }
    auto* out = reinterpret_cast<std::uint16_t*>(output->host);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < kChannels; ++c) {
                const auto output_offset = (y * output->dim[1].stride) +
                                           (x * output->dim[0].stride) +
                                           (c * output->dim[2].stride);
                out[output_offset] = image[pixel_offset(width, x, y, c)];
            }
        }
    }
    return true;
}

bool checked_param_blob(const void* param_blob, std::size_t param_blob_size,
                        const Opcode3DispatchRecord** records, const Opcode3BadPoint** points,
                        const Opcode3BadRect** rects, std::uint32_t* opcode_count) {
    if (param_blob == nullptr || param_blob_size < sizeof(Opcode3DispatchHeader)) {
        return false;
    }
    const auto* base = static_cast<const std::byte*>(param_blob);
    const auto* header = reinterpret_cast<const Opcode3DispatchHeader*>(base);
    const auto records_bytes =
        static_cast<std::size_t>(header->opcode_count) * sizeof(Opcode3DispatchRecord);
    if (param_blob_size < sizeof(Opcode3DispatchHeader) + records_bytes) {
        return false;
    }
    const auto* local_records =
        reinterpret_cast<const Opcode3DispatchRecord*>(base + sizeof(Opcode3DispatchHeader));

    std::size_t point_count = 0;
    std::size_t rect_count = 0;
    for (std::uint32_t i = 0; i < header->opcode_count; ++i) {
        point_count =
            std::max(point_count, static_cast<std::size_t>(local_records[i].point_offset) +
                                      local_records[i].point_count);
        rect_count = std::max(rect_count, static_cast<std::size_t>(local_records[i].rect_offset) +
                                              local_records[i].rect_count);
    }
    const auto points_offset = sizeof(Opcode3DispatchHeader) + records_bytes;
    const auto points_bytes = point_count * sizeof(Opcode3BadPoint);
    const auto rects_offset = points_offset + points_bytes;
    const auto rects_bytes = rect_count * sizeof(Opcode3BadRect);
    if (param_blob_size < rects_offset + rects_bytes) {
        return false;
    }

    *records = local_records;
    *points = reinterpret_cast<const Opcode3BadPoint*>(base + points_offset);
    *rects = reinterpret_cast<const Opcode3BadRect*>(base + rects_offset);
    *opcode_count = header->opcode_count;
    return true;
}

int lens_dng_opcode_list_3_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                                 halide_buffer_t* const* outputs, std::size_t n_outputs,
                                 const void* param_blob, std::size_t param_blob_size) {
    if (n_inputs != 1 || n_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] == nullptr || outputs[0] == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    const Opcode3DispatchRecord* records = nullptr;
    const Opcode3BadPoint* points = nullptr;
    const Opcode3BadRect* rects = nullptr;
    std::uint32_t opcode_count = 0;
    if (!checked_param_blob(param_blob, param_blob_size, &records, &points, &rects,
                            &opcode_count)) {
        return CPIPE_BAD_INDEX;
    }

    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto width = input->dim[0].extent;
    const auto height = input->dim[1].extent;
    if (width <= 0 || height <= 0 || output->dim[0].extent != width ||
        output->dim[1].extent != height) {
        return CPIPE_BAD_INDEX;
    }

    std::vector<std::uint16_t> working;
    if (!read_image(input, width, height, &working)) {
        return CPIPE_BAD_INDEX;
    }
    std::vector<std::uint16_t> next = working;

    for (std::uint32_t i = 0; i < opcode_count; ++i) {
        const auto& record = records[i];
        switch (record.id) {
            case kOpcodeUnknown:
                if (record.optional == 0U) {
                    return CPIPE_UNSUPPORTED;
                }
                continue;
            case kOpcodeWarpRectilinear:
                apply_warp(record, working, width, height, &next);
                break;
            case kOpcodeFixVignetteRadial:
                apply_vignette(record, working, width, height, &next);
                break;
            case kOpcodeFixBadPixelsConstant:
                apply_bad_pixels_constant(record, working, width, height, &next);
                break;
            case kOpcodeFixBadPixelsList:
                apply_bad_pixels_list(record, working, width, height, points, rects, &next);
                break;
            case kOpcodeTrimBounds:
                apply_trim_bounds(record, working, width, height, &next);
                break;
            default:
                return CPIPE_UNSUPPORTED;
        }
        working.swap(next);
    }

    if (!write_image(working, output, width, height)) {
        return CPIPE_BAD_INDEX;
    }
    return CPIPE_OK;
}

}  // namespace

CPIPE_REGISTER_HALIDE_PARAM_FILTER("lens_dng_opcode_list_3", &lens_dng_opcode_list_3_param)

void cpipe_link_builtin_lens_dng_opcode_list_3_halide() {}
