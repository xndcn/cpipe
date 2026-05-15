// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class SharpenEdgeAwareUsm final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.sharpen.edge_aware_usm";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies edge-aware unsharp masking via guided-filter blur per
    /// docs/research/07-classic-isp-algorithms.md §3.7 and He et al. 2010/2013.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "sharpen.edge_aware_usm missing buffers"});
        }

        const auto submitted = compute.submit_halide("sharpen_edge_aware_usm", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("sharpen.edge_aware_usm");
    }
};

}  // namespace cpipe::nodes

extern const char SHARPEN_EDGE_AWARE_USM_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::SharpenEdgeAwareUsm, SHARPEN_EDGE_AWARE_USM_MANIFEST_JSON)

void cpipe_link_builtin_sharpen_edge_aware_usm_halide();

void cpipe_link_builtin_sharpen_edge_aware_usm() {
    cpipe_link_builtin_sharpen_edge_aware_usm_halide();
}
