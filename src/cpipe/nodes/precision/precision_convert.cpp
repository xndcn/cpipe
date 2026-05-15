// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>
#include <string_view>

namespace cpipe::nodes {
namespace {

std::string_view filter_for(compute::PixelFormat input, std::string_view target) {
    using compute::PixelFormat;
    if (input == PixelFormat::R16_UINT && target == "R32_SFLOAT") {
        return "precision_convert_r16u_to_f32";
    }
    if (input == PixelFormat::R32_SFLOAT && target == "R16G16B16A16_SFLOAT") {
        return "precision_convert_f32_to_rgba16f";
    }
    if (input == PixelFormat::R16G16B16A16_SFLOAT && target == "R8G8B8A8_UNORM") {
        return "precision_convert_rgba16f_to_rgba8";
    }
    if (input == PixelFormat::R16G16B16A16_SFLOAT && target == "R16G16B16A16_UNORM") {
        return "precision_convert_rgba16f_to_rgba16u";
    }
    return {};
}

bool target_matches_output(compute::PixelFormat output, std::string_view target) {
    using compute::PixelFormat;
    return (output == PixelFormat::R32_SFLOAT && target == "R32_SFLOAT") ||
           (output == PixelFormat::R16G16B16A16_SFLOAT && target == "R16G16B16A16_SFLOAT") ||
           (output == PixelFormat::R8G8B8A8_UNORM && target == "R8G8B8A8_UNORM") ||
           (output == PixelFormat::R16G16B16A16_UNORM && target == "R16G16B16A16_UNORM");
}

}  // namespace

class PrecisionConvert final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.precision_convert";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "precision_convert missing buffers"});
        }

        const auto target = params.string("target_format");
        if (!target) {
            return tl::unexpected(target.error());
        }
        const auto input_format = inputs[0]->format();
        const auto output_format = outputs[0]->format();
        if (!input_format || !output_format) {
            return tl::unexpected(!input_format ? input_format.error() : output_format.error());
        }
        if (!target_matches_output(static_cast<compute::PixelFormat>(*output_format), *target)) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_PRECISION, "precision_convert target/output mismatch"});
        }

        const auto filter = filter_for(static_cast<compute::PixelFormat>(*input_format), *target);
        if (filter.empty()) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_PRECISION, "precision_convert unsupported conversion"});
        }

        const auto submitted = compute.submit_halide(filter, inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        return out_metadata[0]->add_applied_step("precision_convert");
    }
};

}  // namespace cpipe::nodes

extern const char PRECISION_CONVERT_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::PrecisionConvert, PRECISION_CONVERT_MANIFEST_JSON)

void cpipe_link_builtin_precision_convert_halide();

void cpipe_link_builtin_precision_convert() {
    cpipe_link_builtin_precision_convert_halide();
}
