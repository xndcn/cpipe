// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string_view>

namespace cpipe::nodes {
namespace {

constexpr std::string_view kInputRole = "scene_linear_rec2020";
constexpr std::string_view kStep = "color.scene_linear_to_display";

struct TargetInfo {
    std::string_view dst_cs;
    std::string_view metadata_role;
    compute::PixelFormat output_format{compute::PixelFormat::UNDEFINED};
};

std::filesystem::path default_ocio_config_path() {
    if (const auto* env = std::getenv("CPIPE_OCIO_CONFIG"); env != nullptr && env[0] != '\0') {
        return std::filesystem::path{env};
    }
    return std::filesystem::path{CPIPE_DEFAULT_OCIO_CONFIG};
}

sdk::Result<TargetInfo> target_info(const sdk::ParamView& params) {
    const auto target = params.string("target");
    if (!target) {
        return tl::unexpected(target.error());
    }
    if (*target == "sRGB") {
        return TargetInfo{.dst_cs = "output_srgb",
                          .metadata_role = "output_srgb",
                          .output_format = compute::PixelFormat::R8G8B8A8_UNORM};
    }
    if (*target == "BT2020-PQ") {
        return TargetInfo{.dst_cs = "output_pq_rec2020",
                          .metadata_role = "output_pq_rec2020",
                          .output_format = compute::PixelFormat::R16G16B16A16_UNORM};
    }
    if (*target == "DisplayP3" || *target == "BT2020-HLG") {
        return tl::unexpected(sdk::Error{CPIPE_UNSUPPORTED, "display target reserved for v1.1"});
    }
    return tl::unexpected(sdk::Error{CPIPE_NEED_PARAM, "unknown display target"});
}

sdk::Result<void> require_buffers(const sdk::Buffer& input, const sdk::Buffer& output,
                                  compute::PixelFormat output_format) {
    const auto input_format = input.format();
    if (!input_format) {
        return tl::unexpected(input_format.error());
    }
    const auto actual_output_format = output.format();
    if (!actual_output_format) {
        return tl::unexpected(actual_output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *actual_output_format != static_cast<int>(output_format)) {
        return tl::unexpected(
            sdk::Error{CPIPE_BAD_PRECISION, "scene_linear_to_display format mismatch"});
    }

    const auto input_dims = input.dims();
    if (!input_dims) {
        return tl::unexpected(input_dims.error());
    }
    const auto output_dims = output.dims();
    if (!output_dims) {
        return tl::unexpected(output_dims.error());
    }
    if (*input_dims != *output_dims || input_dims->size() != 2) {
        return tl::unexpected(
            sdk::Error{CPIPE_BAD_INDEX, "scene_linear_to_display dimension mismatch"});
    }
    if (input.cs_role() != kInputRole) {
        return tl::unexpected(
            sdk::Error{CPIPE_NEED_METADATA, "scene_linear_to_display needs Rec2020 scene linear"});
    }
    return {};
}

}  // namespace

class SceneLinearToDisplay final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.color.scene_linear_to_display";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "scene_linear_to_display missing buffers"});
        }

        const auto target = target_info(params);
        if (!target) {
            return tl::unexpected(target.error());
        }
        if (const auto valid = require_buffers(*inputs[0], *outputs[0], target->output_format);
            !valid) {
            return tl::unexpected(valid.error());
        }

        const auto config_path = default_ocio_config_path().string();
        auto* processor = compute.get_ocio_processor(config_path, kInputRole, target->dst_cs);
        if (processor == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_FAILED, "scene_linear_to_display OCIO processor unavailable"});
        }

        const auto submitted = compute.submit_ocio_processor(processor, *inputs[0], *outputs[0]);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        if (const auto set_role = out_metadata[0]->set_cs_role(target->metadata_role); !set_role) {
            return tl::unexpected(set_role.error());
        }
        return out_metadata[0]->add_applied_step(kStep);
    }
};

}  // namespace cpipe::nodes

extern const char COLOR_SCENE_LINEAR_TO_DISPLAY_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::SceneLinearToDisplay, COLOR_SCENE_LINEAR_TO_DISPLAY_MANIFEST_JSON)

void cpipe_link_builtin_color_scene_linear_to_display() {}
