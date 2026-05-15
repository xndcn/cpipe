// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {
namespace {

sdk::Result<void> require_rgba16_stack(std::span<const sdk::Buffer*> inputs,
                                       const sdk::Buffer& output) {
    const auto output_format = output.format();
    if (!output_format) {
        return tl::unexpected(output_format.error());
    }
    if (*output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(
            sdk::Error{CPIPE_BAD_PRECISION, "tone.mertens_local output format mismatch"});
    }

    const auto output_dims = output.dims();
    if (!output_dims) {
        return tl::unexpected(output_dims.error());
    }
    for (const auto* input : inputs) {
        const auto input_format = input->format();
        if (!input_format) {
            return tl::unexpected(input_format.error());
        }
        if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_PRECISION, "tone.mertens_local input format mismatch"});
        }
        const auto input_dims = input->dims();
        if (!input_dims) {
            return tl::unexpected(input_dims.error());
        }
        if (*input_dims != *output_dims) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "tone.mertens_local dimension mismatch"});
        }
    }
    return {};
}

}  // namespace

class ToneMertensLocal final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.tone.mertens_local";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the SDR Mertens exposure-fusion weighting model selected in
    /// docs/research/07-classic-isp-algorithms.md §3.5 / §4.4 from Mertens 2007
    /// and the IPOL 2018 reference family.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 3 || outputs.size() != 1 || inputs[0] == nullptr ||
            inputs[1] == nullptr || inputs[2] == nullptr || outputs[0] == nullptr ||
            out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "tone.mertens_local missing buffers"});
        }
        if (const auto valid = require_rgba16_stack(inputs, *outputs[0]); !valid) {
            return tl::unexpected(valid.error());
        }

        const auto submitted =
            compute.submit_halide_with_params("tone_mertens_local", inputs, outputs, {});
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("tone_mertens_local");
    }
};

}  // namespace cpipe::nodes

extern const char TONE_MERTENS_LOCAL_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::ToneMertensLocal, TONE_MERTENS_LOCAL_MANIFEST_JSON)

void cpipe_link_builtin_tone_mertens_local_halide();

void cpipe_link_builtin_tone_mertens_local() {
    cpipe_link_builtin_tone_mertens_local_halide();
}
