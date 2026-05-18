// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../ParamUtils.hpp"

namespace cpipe::nodes {
namespace {

struct GuidedFilterParams {
    std::int32_t radius;
    float eps;
};

}  // namespace

class DenoiseGuidedFilter final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.denoise.guided_filter";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the edge-preserving guided filter from
    /// docs/research/07-classic-isp-algorithms.md §3.3 / §4.3 and He et al. 2010.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "denoise.guided_filter missing buffers"});
        }

        const GuidedFilterParams guided_params{
            .radius = clamped_param_int_or(params, "radius", 1, 1, 32),
            .eps = clamped_param_float_or(params, "eps", 0.015F, 0.00001F, 0.1F)};
        const auto bytes = std::as_bytes(std::span<const GuidedFilterParams>{&guided_params, 1});
        const auto submitted =
            compute.submit_halide_with_params("denoise_guided_filter", inputs, outputs, bytes);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("denoise.guided_filter");
    }
};

}  // namespace cpipe::nodes

extern const char DENOISE_GUIDED_FILTER_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DenoiseGuidedFilter, DENOISE_GUIDED_FILTER_MANIFEST_JSON)

void cpipe_link_builtin_denoise_guided_filter_halide();

void cpipe_link_builtin_denoise_guided_filter() {
    cpipe_link_builtin_denoise_guided_filter_halide();
}
