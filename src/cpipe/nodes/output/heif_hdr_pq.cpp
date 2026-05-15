// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/color/HeifWriter.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cpipe::nodes {
namespace {

sdk::Result<std::vector<std::uint32_t>> checked_input(const sdk::Buffer& input) {
    const auto format = input.format();
    if (!format) {
        return tl::unexpected(format.error());
    }
    if (*format != static_cast<int>(compute::PixelFormat::R16G16B16A16_UNORM)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "heif_hdr_pq format mismatch"});
    }

    auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "heif_hdr_pq needs Image2D input"});
    }
    return dims;
}

}  // namespace

class OutputHeifHdrPq final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.output.heif_hdr_pq";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*>) override {
        if (inputs.size() != 1 || inputs[0] == nullptr || !outputs.empty()) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "heif_hdr_pq expects one input"});
        }

        const auto dims = checked_input(*inputs[0]);
        if (!dims) {
            return tl::unexpected(dims.error());
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr || metadata->cs_role() != "output_pq_rec2020" ||
            !metadata->has_step("color.scene_linear_to_display")) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "heif_hdr_pq needs output_pq_rec2020"});
        }

        const auto path = params.string("path");
        if (!path) {
            return tl::unexpected(path.error());
        }

        const auto input_lock = inputs[0]->lock_cpu(sdk::CpuAccess::Read);
        if (!input_lock) {
            return tl::unexpected(input_lock.error());
        }

        std::string error;
        const color::Rgba16ImageView view{
            .pixels = static_cast<const std::uint16_t*>(*input_lock),
            .width = (*dims)[0],
            .height = (*dims)[1],
            .stride_pixels = (*dims)[0],
        };
        const color::HeifWriteOptions options{.quality = 58};
        const auto status = color::write_heif_hdr_pq(std::filesystem::path{std::string{*path}},
                                                     view, options, &error);
        (void)inputs[0]->unlock_cpu();
        if (status != CPIPE_OK) {
            return tl::unexpected(sdk::Error{status, error});
        }
        return {};
    }
};

}  // namespace cpipe::nodes

extern const char OUTPUT_HEIF_HDR_PQ_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::OutputHeifHdrPq, OUTPUT_HEIF_HDR_PQ_MANIFEST_JSON)

void cpipe_link_builtin_output_heif_hdr_pq() {}
