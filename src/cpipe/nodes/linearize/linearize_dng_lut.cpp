// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>
#include <span>

namespace cpipe::nodes {

class LinearizeDngLut final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.linearize.dng_lut";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies the DNG raw-domain linearization stage described with black/white
    /// normalization in docs/research/07-classic-isp-algorithms.md §3.9.
    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "linearize missing buffers"});
        }

        const auto input_format = inputs[0]->format();
        const auto output_format = outputs[0]->format();
        if (!input_format || !output_format) {
            return tl::unexpected(!input_format ? input_format.error() : output_format.error());
        }
        if (*input_format != static_cast<int>(compute::PixelFormat::R16_UINT) ||
            *output_format != static_cast<int>(compute::PixelFormat::R32_SFLOAT)) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "linearize format mismatch"});
        }

        const auto dims = inputs[0]->dims();
        if (!dims) {
            return tl::unexpected(dims.error());
        }
        if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "linearize needs Image2D input"});
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "linearize missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (calibration->linearization_table.empty()) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "linearize missing LinearizationTable"});
        }

        const auto input_lock = inputs[0]->lock_cpu(sdk::CpuAccess::Read);
        if (!input_lock) {
            return tl::unexpected(input_lock.error());
        }
        const auto output_lock = outputs[0]->lock_cpu(sdk::CpuAccess::Write);
        if (!output_lock) {
            (void)inputs[0]->unlock_cpu();
            return tl::unexpected(output_lock.error());
        }

        const auto* in = static_cast<const std::uint16_t*>(*input_lock);
        auto* out = static_cast<float*>(*output_lock);
        const auto pixel_count =
            static_cast<std::size_t>((*dims)[0]) * static_cast<std::size_t>((*dims)[1]);
        const auto last = calibration->linearization_table.size() - 1U;
        for (std::size_t i = 0; i < pixel_count; ++i) {
            const auto index = std::min<std::size_t>(in[i], last);
            out[i] = static_cast<float>(calibration->linearization_table[index]);
        }

        (void)outputs[0]->unlock_cpu();
        (void)outputs[0]->flush_cpu_writes();
        (void)inputs[0]->unlock_cpu();

        return out_metadata[0]->add_applied_step("linearization");
    }
};

}  // namespace cpipe::nodes

extern const char LINEARIZE_DNG_LUT_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::LinearizeDngLut, LINEARIZE_DNG_LUT_MANIFEST_JSON)

void cpipe_link_builtin_linearize_dng_lut() {}
