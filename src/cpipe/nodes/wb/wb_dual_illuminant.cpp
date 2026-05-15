// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>
#include <span>

#include "../detail/P1ParamDispatch.hpp"

namespace cpipe::nodes {
namespace {

sdk::Result<std::vector<std::uint32_t>> checked_rgba16_image(const sdk::Buffer& input,
                                                             const sdk::Buffer& output,
                                                             std::string_view node_name) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(
            sdk::Error{CPIPE_BAD_PRECISION, std::string{node_name} + " format mismatch"});
    }

    auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(
            sdk::Error{CPIPE_BAD_INDEX, std::string{node_name} + " needs Image2D input"});
    }
    return dims;
}

}  // namespace

class WbDualIlluminant final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.wb.dual_illuminant";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the P1 DNG AsShotNeutral gain slice from
    /// docs/research/07-classic-isp-algorithms.md §3.3.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "wb missing buffers"});
        }

        const auto dims = checked_rgba16_image(*inputs[0], *outputs[0], "wb");
        if (!dims) {
            return tl::unexpected(dims.error());
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "wb missing metadata"});
        }
        const auto capture = metadata->capture();
        if (!capture) {
            return tl::unexpected(capture.error());
        }
        for (const auto neutral : capture->as_shot_neutral) {
            if (!std::isfinite(neutral) || neutral <= 0.0F) {
                return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "wb invalid AsShotNeutral"});
            }
        }

        detail::WbParams params{};
        std::copy(std::begin(capture->as_shot_neutral), std::end(capture->as_shot_neutral),
                  std::begin(params.as_shot_neutral));
        const auto param_blob = std::as_bytes(std::span{&params, 1});
        const auto submitted =
            compute.submit_halide_with_params("wb_dual_illuminant", inputs, outputs, param_blob);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }

        return out_metadata[0]->add_applied_step("white_balance");
    }
};

}  // namespace cpipe::nodes

extern const char WB_DUAL_ILLUMINANT_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::WbDualIlluminant, WB_DUAL_ILLUMINANT_MANIFEST_JSON)

void cpipe_link_builtin_wb_dual_illuminant_halide();

void cpipe_link_builtin_wb_dual_illuminant() {
    cpipe_link_builtin_wb_dual_illuminant_halide();
}
