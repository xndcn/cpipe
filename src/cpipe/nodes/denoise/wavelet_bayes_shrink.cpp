// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <span>

#include "../ParamUtils.hpp"

namespace cpipe::nodes {
namespace {

struct WaveletBayesShrinkParams {
    float chroma_strength;
};

}  // namespace

class DenoiseWaveletBayesShrink final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.denoise.wavelet_bayes_shrink";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies chroma Haar soft-thresholding based on BayesShrink from
    /// docs/research/07-classic-isp-algorithms.md §4.3 and Chang et al. 2000.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "denoise.wavelet_bayes_shrink missing buffers"});
        }

        const WaveletBayesShrinkParams wavelet_params{
            .chroma_strength = clamped_param_float_or(params, "chroma_strength", 1.0F, 0.0F, 2.0F)};
        const auto bytes =
            std::as_bytes(std::span<const WaveletBayesShrinkParams>{&wavelet_params, 1});
        const auto submitted = compute.submit_halide_with_params("denoise_wavelet_bayes_shrink",
                                                                 inputs, outputs, bytes);
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
