// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class DemosaicBilinear final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.demosaic.bilinear";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || out_metadata.empty() ||
            out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "demosaic missing buffers"});
        }

        const auto* metadata = inputs[0] == nullptr ? nullptr : inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "demosaic missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (!calibration->has_cfa || calibration->cfa_repeat[0] != 2 ||
            calibration->cfa_repeat[1] != 2 || calibration->cfa_pattern[0] != 0 ||
            calibration->cfa_pattern[1] != 1 || calibration->cfa_pattern[2] != 1 ||
            calibration->cfa_pattern[3] != 2) {
            return tl::unexpected(
                sdk::Error{CPIPE_UNSUPPORTED, "demosaic.bilinear requires RGGB CFA"});
        }

        const auto submitted = compute.submit_halide("demosaic_bilinear", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        if (auto cleared = out_metadata[0]->clear_cfa(); !cleared) {
            return cleared;
        }
        return out_metadata[0]->add_applied_step("demosaic");
    }
};

}  // namespace cpipe::nodes

extern const char DEMOSAIC_BILINEAR_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DemosaicBilinear, DEMOSAIC_BILINEAR_MANIFEST_JSON)

void cpipe_link_builtin_demosaic_bilinear() {}
