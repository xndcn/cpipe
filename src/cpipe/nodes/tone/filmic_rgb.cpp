// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <span>

#include "../ParamUtils.hpp"

namespace cpipe::nodes {
namespace {

struct FilmicRgbParams {
    float ev;
    float contrast;
    float saturation;
    float highlights;
    float shadows;
};

FilmicRgbParams read_params(const sdk::ParamView& params) {
    return {
        .ev = clamped_param_float_or(params, "ev", 0.0F, -2.0F, 2.0F),
        .contrast = clamped_param_float_or(params, "contrast", 1.0F, 0.5F, 2.0F),
        .saturation = clamped_param_float_or(params, "saturation", 1.0F, 0.0F, 2.0F),
        .highlights = clamped_param_float_or(params, "highlights", 1.0F, 0.0F, 2.0F),
        .shadows = clamped_param_float_or(params, "shadows", 1.0F, 0.0F, 2.0F),
    };
}

}  // namespace

class ToneFilmicRgb final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.tone.filmic_rgb";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the fixed filmic-RGB-style global tone curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 / §4.4.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "tone.filmic_rgb missing buffers"});
        }

        const auto filmic_params = read_params(params);
        const auto bytes = std::as_bytes(std::span<const FilmicRgbParams>{&filmic_params, 1});
        const auto submitted =
            compute.submit_halide_with_params("tone_filmic_rgb", inputs, outputs, bytes);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("tone.filmic_rgb");
    }
};

}  // namespace cpipe::nodes

extern const char TONE_FILMIC_RGB_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ToneFilmicRgb, TONE_FILMIC_RGB_MANIFEST_JSON)

void cpipe_link_builtin_tone_filmic_rgb_halide();

void cpipe_link_builtin_tone_filmic_rgb() {
    cpipe_link_builtin_tone_filmic_rgb_halide();
}
