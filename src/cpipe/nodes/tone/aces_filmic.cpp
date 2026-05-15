// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class ToneAcesFilmic final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.tone.aces_filmic";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the Narkowicz 2016 ACES filmic fit selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 as a global tone-map option.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "tone.aces_filmic missing buffers"});
        }

        const auto submitted = compute.submit_halide("tone_aces_filmic", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("tone.aces_filmic");
    }
};

}  // namespace cpipe::nodes

extern const char TONE_ACES_FILMIC_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ToneAcesFilmic, TONE_ACES_FILMIC_MANIFEST_JSON)

void cpipe_link_builtin_tone_aces_filmic_halide();

void cpipe_link_builtin_tone_aces_filmic() {
    cpipe_link_builtin_tone_aces_filmic_halide();
}
