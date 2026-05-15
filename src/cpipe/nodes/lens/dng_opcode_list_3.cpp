// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList3.hpp>
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

constexpr std::string_view kOpcodeList3BlobKey{"com.cpipe.dng.opcode_list_3_bytes"};

template <class T>
void append_struct(std::vector<std::byte>* out, const T& value) {
    const auto bytes = std::as_bytes(std::span{&value, 1});
    out->insert(out->end(), bytes.begin(), bytes.end());
}

sdk::Result<void> checked_rgba16_pair(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "OpcodeList3 format mismatch"});
    }
    const auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "OpcodeList3 needs Image2D input"});
    }
    return {};
}

sdk::Result<void> copy_rgba16_image(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    const auto input_lock = input.lock_cpu(sdk::CpuAccess::Read);
    if (!input_lock) {
        return tl::unexpected(input_lock.error());
    }
    const auto output_lock = output.lock_cpu(sdk::CpuAccess::Write);
    if (!output_lock) {
        (void)input.unlock_cpu();
        return tl::unexpected(output_lock.error());
    }

    const auto byte_count = static_cast<std::size_t>((*dims)[0]) *
                            static_cast<std::size_t>((*dims)[1]) * 4U * sizeof(std::uint16_t);
    std::memcpy(*output_lock, *input_lock, byte_count);
    (void)input.unlock_cpu();
    auto unlock_status = output.unlock_cpu();
    if (!unlock_status) {
        return unlock_status;
    }
    return output.flush_cpu_writes();
}

void pack_warp(const ingest::dng_opcode::OpcodeList3::WarpRectilinear& warp,
               detail::Opcode3DispatchRecord* record) {
    record->coefficient_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(warp.coefficients.size(), std::size(record->coefficients)));
    for (std::uint32_t i = 0; i < record->coefficient_count; ++i) {
        record->coefficients[i] = detail::Opcode3WarpCoefficient{
            .kr0 = warp.coefficients[i].kr0,
            .kr1 = warp.coefficients[i].kr1,
            .kr2 = warp.coefficients[i].kr2,
            .kr3 = warp.coefficients[i].kr3,
            .kt0 = warp.coefficients[i].kt0,
            .kt1 = warp.coefficients[i].kt1,
        };
    }
    record->cx_hat = warp.cx_hat;
    record->cy_hat = warp.cy_hat;
}

void pack_vignette(const ingest::dng_opcode::OpcodeList3::FixVignetteRadial& vignette,
                   detail::Opcode3DispatchRecord* record) {
    for (std::size_t i = 0; i < vignette.k.size(); ++i) {
        record->vignette_k[i] = vignette.k[i];
    }
    record->vignette_cx_hat = vignette.cx_hat;
    record->vignette_cy_hat = vignette.cy_hat;
}

std::vector<std::byte> pack_opcode_list_3(
    const std::vector<ingest::dng_opcode::OpcodeList3::Opcode>& opcodes) {
    detail::Opcode3DispatchHeader header{};
    header.opcode_count = static_cast<std::uint32_t>(opcodes.size());

    std::vector<detail::Opcode3DispatchRecord> records;
    std::vector<detail::Opcode3BadPoint> points;
    std::vector<detail::Opcode3BadRect> rects;
    records.reserve(opcodes.size());
    for (const auto& opcode : opcodes) {
        detail::Opcode3DispatchRecord record{};
        record.id = static_cast<std::uint32_t>(opcode.id);
        record.optional = opcode.optional ? 1U : 0U;

        if (opcode.warp_rectilinear) {
            pack_warp(*opcode.warp_rectilinear, &record);
        }
        if (opcode.fix_vignette_radial) {
            pack_vignette(*opcode.fix_vignette_radial, &record);
        }
        if (opcode.fix_bad_pixels_constant) {
            record.constant = opcode.fix_bad_pixels_constant->constant;
            record.bayer_phase = opcode.fix_bad_pixels_constant->bayer_phase;
        }
        if (opcode.fix_bad_pixels_list) {
            record.bayer_phase = opcode.fix_bad_pixels_list->bayer_phase;
            record.point_offset = static_cast<std::uint32_t>(points.size());
            record.point_count =
                static_cast<std::uint32_t>(opcode.fix_bad_pixels_list->bad_points.size());
            for (const auto& point : opcode.fix_bad_pixels_list->bad_points) {
                points.push_back(detail::Opcode3BadPoint{.row = point.row, .column = point.column});
            }
            record.rect_offset = static_cast<std::uint32_t>(rects.size());
            record.rect_count =
                static_cast<std::uint32_t>(opcode.fix_bad_pixels_list->bad_rects.size());
            for (const auto& rect : opcode.fix_bad_pixels_list->bad_rects) {
                rects.push_back(detail::Opcode3BadRect{.top = rect.top,
                                                       .left = rect.left,
                                                       .bottom = rect.bottom,
                                                       .right = rect.right});
            }
        }
        if (opcode.trim_bounds) {
            record.top = opcode.trim_bounds->top;
            record.left = opcode.trim_bounds->left;
            record.bottom = opcode.trim_bounds->bottom;
            record.right = opcode.trim_bounds->right;
        }
        records.push_back(record);
    }

    std::vector<std::byte> out;
    out.reserve(sizeof(header) + (records.size() * sizeof(detail::Opcode3DispatchRecord)) +
                (points.size() * sizeof(detail::Opcode3BadPoint)) +
                (rects.size() * sizeof(detail::Opcode3BadRect)));
    append_struct(&out, header);
    for (const auto& record : records) {
        append_struct(&out, record);
    }
    for (const auto& point : points) {
        append_struct(&out, point);
    }
    for (const auto& rect : rects) {
        append_struct(&out, rect);
    }
    return out;
}

}  // namespace

class LensDngOpcodeList3 final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.lens.dng_opcode_list_3";
    static constexpr const char* VERSION = "1.0.0";

    /// Applies DNG OpcodeList3 RGB-domain lens and bad-pixel corrections per
    /// docs/research/07-classic-isp-algorithms.md §3.8 and
    /// docs/research/12-dng-format.md §3.5.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "OpcodeList3 missing buffers"});
        }
        if (auto checked = checked_rgba16_pair(*inputs[0], *outputs[0]); !checked) {
            return checked;
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "OpcodeList3 missing metadata"});
        }
        const auto opcode_list_3 = metadata->blob(kOpcodeList3BlobKey);
        if (!opcode_list_3) {
            if (auto copied = copy_rgba16_image(*inputs[0], *outputs[0]); !copied) {
                return copied;
            }
            return out_metadata[0]->add_applied_step("opcode_list_3");
        }
        auto parsed = ingest::dng_opcode::OpcodeList3::parse(*opcode_list_3);
        if (parsed.status != CPIPE_OK) {
            return tl::unexpected(sdk::Error{parsed.status, parsed.message});
        }
        if (parsed.opcodes.empty()) {
            if (auto copied = copy_rgba16_image(*inputs[0], *outputs[0]); !copied) {
                return copied;
            }
            return out_metadata[0]->add_applied_step("opcode_list_3");
        }

        const auto param_blob = pack_opcode_list_3(parsed.opcodes);
        const auto submitted = compute.submit_halide_with_params("lens_dng_opcode_list_3", inputs,
                                                                 outputs, param_blob);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }

        return out_metadata[0]->add_applied_step("opcode_list_3");
    }
};

}  // namespace cpipe::nodes

extern const char LENS_DNG_OPCODE_LIST_3_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::LensDngOpcodeList3, LENS_DNG_OPCODE_LIST_3_MANIFEST_JSON)

void cpipe_link_builtin_lens_dng_opcode_list_3_halide();

void cpipe_link_builtin_lens_dng_opcode_list_3() {
    cpipe_link_builtin_lens_dng_opcode_list_3_halide();
}
