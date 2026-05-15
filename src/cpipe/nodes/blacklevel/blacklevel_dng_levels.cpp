// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>
#include <span>

#include "../detail/P1ParamDispatch.hpp"

namespace cpipe::nodes {

class BlacklevelDngLevels final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.blacklevel.dng_levels";
    static constexpr const char* VERSION = "1.0.0";

    /// Performs the DNG black/white linear scale from
    /// docs/research/07-classic-isp-algorithms.md §3.9.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "blacklevel missing buffers"});
        }

        const auto input_format = inputs[0]->format();
        const auto output_format = outputs[0]->format();
        if (!input_format || !output_format) {
            return tl::unexpected(!input_format ? input_format.error() : output_format.error());
        }
        if (*input_format != static_cast<int>(compute::PixelFormat::R32_SFLOAT) ||
            *output_format != static_cast<int>(compute::PixelFormat::R32_SFLOAT)) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "blacklevel format mismatch"});
        }

        const auto dims = inputs[0]->dims();
        if (!dims) {
            return tl::unexpected(dims.error());
        }
        if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "blacklevel needs Image2D input"});
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "blacklevel missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (!calibration->has_cfa || calibration->cfa_repeat[0] != 2 ||
            calibration->cfa_repeat[1] != 2 || calibration->white_level == 0) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "blacklevel missing DNG levels"});
        }

        detail::BlacklevelParams params{};
        std::copy(std::begin(calibration->black_level), std::end(calibration->black_level),
                  std::begin(params.black_level));
        params.white_level = calibration->white_level;
        std::copy_n(calibration->cfa_pattern.begin(), 4U, params.cfa_pattern);
        const auto param_blob = std::as_bytes(std::span{&params, 1});
        const auto submitted =
            compute.submit_halide_with_params("blacklevel_dng_levels", inputs, outputs, param_blob);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }

        return out_metadata[0]->add_applied_step("black_white_scaling");
    }
};

}  // namespace cpipe::nodes

extern const char BLACKLEVEL_DNG_LEVELS_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::BlacklevelDngLevels, BLACKLEVEL_DNG_LEVELS_MANIFEST_JSON)

void cpipe_link_builtin_blacklevel_dng_levels_halide();

void cpipe_link_builtin_blacklevel_dng_levels() {
    cpipe_link_builtin_blacklevel_dng_levels_halide();
}
