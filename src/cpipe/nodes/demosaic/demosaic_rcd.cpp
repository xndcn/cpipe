// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

#include "DemosaicCommon.hpp"

namespace cpipe::nodes {

class DemosaicRcd final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.demosaic.rcd";
    static constexpr const char* VERSION = "1.0.0";

    /// Re-implements the Ratio Corrected Demosaicing structure from
    /// docs/research/07-classic-isp-algorithms.md §4.1 and Sanz Rodríguez &
    /// Bayón 2014; no GPL RawTherapee/darktable source was consulted.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || out_metadata.empty() ||
            out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "demosaic.rcd missing buffers"});
        }

        const auto* metadata = inputs[0] == nullptr ? nullptr : inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "demosaic.rcd missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (!detail::is_supported_bayer_cfa(*calibration)) {
            return tl::unexpected(
                sdk::Error{CPIPE_UNSUPPORTED, "demosaic.rcd requires Bayer 2x2 CFA"});
        }

        const auto params = detail::make_demosaic_params(*calibration);
        const auto param_blob = std::as_bytes(std::span{&params, 1});
        const auto submitted =
            compute.submit_halide_with_params("demosaic_rcd", inputs, outputs, param_blob);
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

extern const char DEMOSAIC_RCD_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DemosaicRcd, DEMOSAIC_RCD_MANIFEST_JSON)

void cpipe_link_builtin_demosaic_rcd_halide();

void cpipe_link_builtin_demosaic_rcd() {
    cpipe_link_builtin_demosaic_rcd_halide();
}
