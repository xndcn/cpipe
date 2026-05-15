// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "../detail/P1ParamDispatch.hpp"

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
constexpr const char* kCameraToXyzBlob = "com.cpipe.wb.camera_to_xyz_d50_f32";

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

sdk::Result<std::array<float, 9>> required_camera_to_xyz_blob(const sdk::BufferMetadata& metadata) {
    const auto blob = metadata.blob(kCameraToXyzBlob);
    if (!blob) {
        return tl::unexpected(
            sdk::Error{CPIPE_NEED_METADATA, "colormatrix missing WB camera-to-XYZ blob"});
    }
    if (blob->size() != 9U * sizeof(float)) {
        return tl::unexpected(
            sdk::Error{CPIPE_NEED_METADATA, "colormatrix invalid WB camera-to-XYZ blob"});
    }

    std::array<float, 9> matrix{};
    std::memcpy(matrix.data(), blob->data(), blob->size());
    for (const auto value : matrix) {
        if (!std::isfinite(value)) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "colormatrix non-finite WB camera-to-XYZ blob"});
        }
    }
    return matrix;
}

}  // namespace

class ColormatrixDngToWorking final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.colormatrix.dng_to_working";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the WB-interpolated camera-to-XYZ → Bradford D50-to-D65 → Rec.2020 chain from
    /// docs/research/07-classic-isp-algorithms.md §3.3 and
    /// docs/research/13-color-management.md §3.6.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
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
        const auto camera_to_xyz_d50 = required_camera_to_xyz_blob(*metadata);
        if (!camera_to_xyz_d50) {
            return tl::unexpected(camera_to_xyz_d50.error());
        }
        const auto transform = mul3(kXyzD65ToRec2020, mul3(kD50ToD65, *camera_to_xyz_d50));

        detail::ColormatrixParams params{};
        std::copy(transform.begin(), transform.end(), std::begin(params.transform));
        const auto param_blob = std::as_bytes(std::span{&params, 1});
        const auto submitted = compute.submit_halide_with_params("colormatrix_dng_to_working",
                                                                 inputs, outputs, param_blob);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }

        if (auto role = out_metadata[0]->set_cs_role("scene_linear_rec2020"); !role) {
            return role;
        }
        return out_metadata[0]->add_applied_step("color_matrix");
    }
};

}  // namespace cpipe::nodes

extern const char COLORMATRIX_DNG_TO_WORKING_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ColormatrixDngToWorking, COLORMATRIX_DNG_TO_WORKING_MANIFEST_JSON)

void cpipe_link_builtin_colormatrix_dng_to_working_halide();

void cpipe_link_builtin_colormatrix_dng_to_working() {
    cpipe_link_builtin_colormatrix_dng_to_working_halide();
}
