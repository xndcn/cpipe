// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>
#include <span>

#include "../detail/Float16.hpp"

namespace cpipe::nodes {
namespace {

constexpr std::array<float, 9> kD50ToD65{
    0.9555766F, -0.0230393F, 0.0631636F,  -0.0282895F, 1.0099416F,
    0.0210077F, 0.0122982F,  -0.0204830F, 1.3299098F,
};

constexpr std::array<float, 9> kXyzD65ToRec2020{
    1.7166512F, -0.3556708F, -0.2533663F, -0.6666844F, 1.6164812F,
    0.0157685F, 0.0176399F,  -0.0427706F, 0.9421031F,
};

sdk::Result<std::vector<std::uint32_t>> checked_rgba16_image(const sdk::Buffer& input,
                                                             const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "colormatrix format mismatch"});
    }

    auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "colormatrix needs Image2D input"});
    }
    return dims;
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

std::optional<std::array<float, 9>> inverse3(const std::array<float, 9>& m) {
    const auto det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!std::isfinite(det) || std::abs(det) < 1.0e-8F) {
        return std::nullopt;
    }
    const auto inv_det = 1.0F / det;
    return std::array<float, 9>{
        (m[4] * m[8] - m[5] * m[7]) * inv_det, (m[2] * m[7] - m[1] * m[8]) * inv_det,
        (m[1] * m[5] - m[2] * m[4]) * inv_det, (m[5] * m[6] - m[3] * m[8]) * inv_det,
        (m[0] * m[8] - m[2] * m[6]) * inv_det, (m[2] * m[3] - m[0] * m[5]) * inv_det,
        (m[3] * m[7] - m[4] * m[6]) * inv_det, (m[1] * m[6] - m[0] * m[7]) * inv_det,
        (m[0] * m[4] - m[1] * m[3]) * inv_det,
    };
}

}  // namespace

class ColormatrixDngToWorking final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.colormatrix.dng_to_working";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the P1 ColorMatrix1 → Bradford D50-to-D65 → Rec.2020 chain from
    /// docs/research/07-classic-isp-algorithms.md §3.3 and
    /// docs/research/13-color-management.md §3.6.
    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*, const sdk::ParamView&,
                              std::span<const sdk::Buffer*> inputs, std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "colormatrix missing buffers"});
        }

        const auto dims = checked_rgba16_image(*inputs[0], *outputs[0]);
        if (!dims) {
            return tl::unexpected(dims.error());
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "colormatrix missing metadata"});
        }
        if (metadata->cs_role() != "raw_camera" || !metadata->has_step("white_balance")) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "colormatrix requires raw_camera white balance"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        const auto& color_matrix1 = calibration->color_matrix1;
        if (!color_matrix1) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "colormatrix missing ColorMatrix1"});
        }
        const auto camera_to_xyz_d50 = inverse3(*color_matrix1);
        if (!camera_to_xyz_d50) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "colormatrix invalid ColorMatrix1"});
        }
        const auto transform = mul3(kXyzD65ToRec2020, mul3(kD50ToD65, *camera_to_xyz_d50));

        const auto input_lock = inputs[0]->lock_cpu(sdk::CpuAccess::Read);
        if (!input_lock) {
            return tl::unexpected(input_lock.error());
        }
        const auto output_lock = outputs[0]->lock_cpu(sdk::CpuAccess::Write);
        if (!output_lock) {
            (void)inputs[0]->unlock_cpu();
            return tl::unexpected(output_lock.error());
        }

        const auto* in = static_cast<const std::uint16_t*>(*input_lock);
        auto* out = static_cast<std::uint16_t*>(*output_lock);
        const auto pixel_count =
            static_cast<std::size_t>((*dims)[0]) * static_cast<std::size_t>((*dims)[1]);
        for (std::size_t i = 0; i < pixel_count; ++i) {
            const auto base = i * 4U;
            const std::array<float, 3> rgb{detail::half_to_float(in[base + 0U]),
                                           detail::half_to_float(in[base + 1U]),
                                           detail::half_to_float(in[base + 2U])};
            const auto mapped = mul3(transform, rgb);
            out[base + 0U] = detail::float_to_half(mapped[0]);
            out[base + 1U] = detail::float_to_half(mapped[1]);
            out[base + 2U] = detail::float_to_half(mapped[2]);
            out[base + 3U] = in[base + 3U];
        }

        (void)outputs[0]->unlock_cpu();
        (void)outputs[0]->flush_cpu_writes();
        (void)inputs[0]->unlock_cpu();

        if (auto role = out_metadata[0]->set_cs_role("scene_linear_rec2020"); !role) {
            return role;
        }
        return out_metadata[0]->add_applied_step("color_matrix");
    }
};

}  // namespace cpipe::nodes

extern const char COLORMATRIX_DNG_TO_WORKING_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ColormatrixDngToWorking, COLORMATRIX_DNG_TO_WORKING_MANIFEST_JSON)

void cpipe_link_builtin_colormatrix_dng_to_working() {}
