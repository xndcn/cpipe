// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <span>

#include "../ParamUtils.hpp"

namespace cpipe::nodes {
namespace {

struct Bm3dParams {
    float sigma;
};

sdk::Result<float> sigma_from_params_or_metadata(const sdk::ParamView& params,
                                                 const sdk::Buffer& input) {
    if (params.suite() != nullptr && params.suite()->get_double != nullptr &&
        params.impl() != nullptr) {
        double sigma_value = 0.0;
        const auto sigma_status = static_cast<cpipe_status_t>(
            params.suite()->get_double(params.impl(), "sigma", &sigma_value));
        if (sigma_status == CPIPE_OK) {
            return std::clamp(static_cast<float>(sigma_value), 0.0F, 0.2F);
        }

        double override_value = 0.0;
        const auto status = static_cast<cpipe_status_t>(
            params.suite()->get_double(params.impl(), "sigma_override", &override_value));
        if (status == CPIPE_OK) {
            return std::clamp(static_cast<float>(override_value), 0.0F, 0.2F);
        }
    }

    const auto* metadata = input.metadata();
    if (metadata == nullptr) {
        return 0.03F;
    }
    const auto calibration = metadata->calibration();
    if (!calibration || calibration->noise_profile.empty()) {
        return 0.03F;
    }

    double variance = 0.0;
    for (const auto& [beta, lambda] : calibration->noise_profile) {
        variance += std::max(0.0F, beta + (lambda * 0.25F));
    }
    variance /= static_cast<double>(calibration->noise_profile.size());
    return static_cast<float>(std::sqrt(variance));
}

sdk::Result<void> require_rgba16(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "denoise.bm3d format mismatch"});
    }
    return {};
}

}  // namespace

class DenoiseBm3d final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.denoise.bm3d";
    static constexpr const char* VERSION = "1.0.0";

    /// Runs the P2 BM3D-style two-stage entry surface selected in
    /// docs/research/07-classic-isp-algorithms.md §3.6 / §4.4 from Dabov 2007 and
    /// the Mäkinen 2020 reference family.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "denoise.bm3d missing buffers"});
        }
        if (const auto valid = require_rgba16(*inputs[0], *outputs[0]); !valid) {
            return tl::unexpected(valid.error());
        }

        const auto sigma = sigma_from_params_or_metadata(params, *inputs[0]);
        if (!sigma) {
            return tl::unexpected(sigma.error());
        }
        const Bm3dParams bm3d_params{.sigma = *sigma};
        const auto bytes = std::as_bytes(std::span<const Bm3dParams>{&bm3d_params, 1});
        const auto submitted =
            compute.submit_halide_with_params("denoise_bm3d_step1", inputs, outputs, bytes);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("denoise.bm3d");
    }
};

}  // namespace cpipe::nodes

extern const char DENOISE_BM3D_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DenoiseBm3d, DENOISE_BM3D_MANIFEST_JSON)

void cpipe_link_builtin_denoise_bm3d_halide();

void cpipe_link_builtin_denoise_bm3d() {
    cpipe_link_builtin_denoise_bm3d_halide();
}
