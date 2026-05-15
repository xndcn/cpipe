// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/color/Cube3dLutLoader.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Lut3dParams.hpp"

namespace cpipe::nodes {
namespace {

sdk::Result<void> require_rgba16_pair(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto input_format = input.format();
    if (!input_format) {
        return tl::unexpected(input_format.error());
    }
    const auto output_format = output.format();
    if (!output_format) {
        return tl::unexpected(output_format.error());
    }
    constexpr auto rgba16 = static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT);
    if (*input_format != rgba16 || *output_format != rgba16) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "color.3d_lut format mismatch"});
    }

    const auto input_dims = input.dims();
    if (!input_dims) {
        return tl::unexpected(input_dims.error());
    }
    const auto output_dims = output.dims();
    if (!output_dims) {
        return tl::unexpected(output_dims.error());
    }
    if (*input_dims != *output_dims) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "color.3d_lut dimension mismatch"});
    }
    return {};
}

std::vector<float> make_param_blob_words(const color::Cube3dLut& lut) {
    detail::Lut3dParamHeader header{.size = lut.size,
                                    .value_count = static_cast<std::uint32_t>(lut.values.size())};
    std::vector<float> words((sizeof(header) / sizeof(float)) + lut.values.size());
    std::memcpy(words.data(), &header, sizeof(header));
    std::copy(lut.values.begin(), lut.values.end(), words.begin() + 2);
    return words;
}

}  // namespace

class Color3dLut final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.color.3d_lut";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies a 3D RGB LUT with tetrahedral interpolation as selected in
    /// docs/research/07-classic-isp-algorithms.md §3.10 and P2-PD-25.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "color.3d_lut missing buffers"});
        }
        if (const auto valid = require_rgba16_pair(*inputs[0], *outputs[0]); !valid) {
            return tl::unexpected(valid.error());
        }

        const auto lut_path = params.string("lut_path");
        if (!lut_path) {
            return tl::unexpected(lut_path.error());
        }

        color::Cube3dLut lut;
        std::string error;
        if (const auto status = color::Cube3dLutLoader::load(
                std::filesystem::path{std::string{*lut_path}}, &lut, &error);
            status != CPIPE_OK) {
            return tl::unexpected(sdk::Error{status, error});
        }

        const auto param_words = make_param_blob_words(lut);
        const auto submitted = compute.submit_halide_with_params(
            "color_3d_lut", inputs, outputs, std::as_bytes(std::span<const float>{param_words}));
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("color.3d_lut");
    }
};

}  // namespace cpipe::nodes

extern const char COLOR_3D_LUT_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::Color3dLut, COLOR_3D_LUT_MANIFEST_JSON)

void cpipe_link_builtin_color_3d_lut_halide();

void cpipe_link_builtin_color_3d_lut() {
    cpipe_link_builtin_color_3d_lut_halide();
}
