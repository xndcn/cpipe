// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class ToneReinhard final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.tone.reinhard";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the Reinhard 2002 global tone-map curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 as the debug tone option.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "tone.reinhard missing buffers"});
        }

        const auto submitted = compute.submit_halide("tone_reinhard", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("tone.reinhard");
    }
};

}  // namespace cpipe::nodes

extern const char TONE_REINHARD_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ToneReinhard, TONE_REINHARD_MANIFEST_JSON)

void cpipe_link_builtin_tone_reinhard_halide();

void cpipe_link_builtin_tone_reinhard() {
    cpipe_link_builtin_tone_reinhard_halide();
}
