// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class DenoiseWaveletBayesShrink final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.denoise.wavelet_bayes_shrink";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies chroma Haar soft-thresholding based on BayesShrink from
    /// docs/research/07-classic-isp-algorithms.md §4.3 and Chang et al. 2000.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "denoise.wavelet_bayes_shrink missing buffers"});
        }

        const auto submitted =
            compute.submit_halide("denoise_wavelet_bayes_shrink", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("denoise.wavelet_bayes_shrink");
    }
};

}  // namespace cpipe::nodes

extern const char DENOISE_WAVELET_BAYES_SHRINK_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DenoiseWaveletBayesShrink,
                    DENOISE_WAVELET_BAYES_SHRINK_MANIFEST_JSON)

void cpipe_link_builtin_denoise_wavelet_bayes_shrink_halide();

void cpipe_link_builtin_denoise_wavelet_bayes_shrink() {
    cpipe_link_builtin_denoise_wavelet_bayes_shrink_halide();
}
