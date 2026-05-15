// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class ToneFilmicRgb final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.tone.filmic_rgb";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the fixed filmic-RGB-style global tone curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 / §4.4.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "tone.filmic_rgb missing buffers"});
        }

        const auto submitted = compute.submit_halide("tone_filmic_rgb", inputs, outputs);
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
