// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList2.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "../detail/P1ParamDispatch.hpp"

namespace cpipe::nodes {
namespace {

constexpr std::string_view kOpcodeList2BlobKey{"com.cpipe.dng.opcode_list_2_bytes"};

template <class T>
void append_struct(std::vector<std::byte>* out, const T& value) {
    const auto bytes = std::as_bytes(std::span{&value, 1});
    out->insert(out->end(), bytes.begin(), bytes.end());
}

sdk::Result<void> checked_r32_pair(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R32_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R32_SFLOAT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "gainmap format mismatch"});
    }
    const auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "gainmap needs Image2D input"});
    }
    return {};
}

sdk::Result<void> validate_gain_maps(const std::vector<ingest::dng_opcode::GainMap>& gain_maps) {
    if (gain_maps.empty()) {
        return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "GainMap opcode missing"});
    }
    if (gain_maps.size() == 1 && gain_maps[0].planes == 1 && gain_maps[0].plane == 0) {
        return {};
    }
    if (gain_maps.size() != 4) {
        return tl::unexpected(sdk::Error{CPIPE_UNSUPPORTED, "GainMap plane set incomplete"});
    }
    std::array<bool, 4> seen{};
    for (const auto& map : gain_maps) {
        if (map.planes != 4 || map.plane >= seen.size() || seen[map.plane]) {
            return tl::unexpected(sdk::Error{CPIPE_UNSUPPORTED, "GainMap plane set invalid"});
        }
        seen[map.plane] = true;
    }
    return {};
}

std::vector<std::byte> pack_gain_maps(const sdk::CalibrationView& calibration,
                                      const std::vector<ingest::dng_opcode::GainMap>& gain_maps) {
    detail::GainMapDispatchHeader header{};
    header.map_count = static_cast<std::uint32_t>(gain_maps.size());
    std::copy(calibration.cfa_repeat.begin(), calibration.cfa_repeat.end(), header.cfa_repeat);
    std::copy(calibration.cfa_pattern.begin(), calibration.cfa_pattern.end(), header.cfa_pattern);

    std::vector<detail::GainMapDispatchPlane> planes;
    planes.reserve(gain_maps.size());
    std::uint32_t gain_offset = 0;
    for (const auto& map : gain_maps) {
        const auto gain_count = static_cast<std::uint32_t>(map.gain.size());
        planes.push_back(detail::GainMapDispatchPlane{.top = map.top,
                                                      .left = map.left,
                                                      .bottom = map.bottom,
                                                      .right = map.right,
                                                      .plane = map.plane,
                                                      .planes = map.planes,
                                                      .map_points_v = map.map_points_v,
                                                      .map_points_h = map.map_points_h,
                                                      .map_spacing_v = map.map_spacing_v,
                                                      .map_spacing_h = map.map_spacing_h,
                                                      .map_origin_v = map.map_origin_v,
                                                      .map_origin_h = map.map_origin_h,
                                                      .gain_offset = gain_offset,
                                                      .gain_count = gain_count});
        gain_offset += gain_count;
    }

    std::vector<std::byte> out;
    out.reserve(sizeof(header) + planes.size() * sizeof(detail::GainMapDispatchPlane) +
                gain_offset * sizeof(float));
    append_struct(&out, header);
    for (const auto& plane : planes) {
        append_struct(&out, plane);
    }
    for (const auto& map : gain_maps) {
        const auto bytes = std::as_bytes(std::span{map.gain});
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

}  // namespace

class LensShadingGainMap final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.lens.shading_gainmap";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies DNG OpcodeList2 GainMap lens shading per
    /// docs/research/07-classic-isp-algorithms.md §3.10 and
    /// docs/research/12-dng-format.md §3.5.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "gainmap missing buffers"});
        }
        if (auto checked = checked_r32_pair(*inputs[0], *outputs[0]); !checked) {
            return checked;
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "gainmap missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (!calibration->has_cfa) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "gainmap missing CFA"});
        }
        const auto opcode_list_2 = metadata->blob(kOpcodeList2BlobKey);
        if (!opcode_list_2) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "OpcodeList2 blob missing"});
        }
        auto parsed = ingest::dng_opcode::OpcodeList2::parse_gain_maps(*opcode_list_2);
        if (parsed.status != CPIPE_OK) {
            return tl::unexpected(sdk::Error{parsed.status, parsed.message});
        }
        if (auto valid = validate_gain_maps(parsed.gain_maps); !valid) {
            return valid;
        }

        const auto param_blob = pack_gain_maps(*calibration, parsed.gain_maps);
        const auto submitted =
            compute.submit_halide_with_params("lens_shading_gainmap", inputs, outputs, param_blob);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }

        return out_metadata[0]->add_applied_step("lens_shading_gainmap");
    }
};

}  // namespace cpipe::nodes

extern const char LENS_SHADING_GAINMAP_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::LensShadingGainMap, LENS_SHADING_GAINMAP_MANIFEST_JSON)

void cpipe_link_builtin_lens_shading_gainmap_halide();

void cpipe_link_builtin_lens_shading_gainmap() {
    cpipe_link_builtin_lens_shading_gainmap_halide();
}
