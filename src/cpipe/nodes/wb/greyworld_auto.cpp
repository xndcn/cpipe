// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../detail/Float16.hpp"
#include "WbMetadata.hpp"

namespace cpipe::nodes {
namespace {

sdk::Result<std::vector<std::uint32_t>> checked_rgba16_image(const sdk::Buffer& input,
                                                             const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16G16B16A16_SFLOAT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "wb.greyworld format mismatch"});
    }

    auto dims = input.dims();
    if (!dims) {
        return tl::unexpected(dims.error());
    }
    if (dims->size() != 2 || (*dims)[0] == 0 || (*dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "wb.greyworld needs Image2D input"});
    }
    return dims;
}

sdk::Result<wb_detail::Vec3> estimate_neutral_from_pixels(const std::uint16_t* pixels,
                                                          std::size_t pixel_count) {
    std::array<double, 3> sums{};
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto base = i * 4U;
        sums[0] += detail::half_to_float(pixels[base + 0U]);
        sums[1] += detail::half_to_float(pixels[base + 1U]);
        sums[2] += detail::half_to_float(pixels[base + 2U]);
    }
    for (const auto sum : sums) {
        if (!std::isfinite(sum) || sum <= 0.0) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "wb.greyworld invalid channel mean"});
        }
    }
    return wb_detail::Vec3{static_cast<float>(sums[0] / sums[1]), 1.0F,
                           static_cast<float>(sums[2] / sums[1])};
}

void apply_neutral(const std::uint16_t* input, std::uint16_t* output, std::size_t pixel_count,
                   const wb_detail::Vec3& neutral) {
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto base = i * 4U;
        output[base + 0U] =
            detail::float_to_half(detail::half_to_float(input[base + 0U]) / neutral[0]);
        output[base + 1U] =
            detail::float_to_half(detail::half_to_float(input[base + 1U]) / neutral[1]);
        output[base + 2U] =
            detail::float_to_half(detail::half_to_float(input[base + 2U]) / neutral[2]);
        output[base + 3U] = input[base + 3U];
    }
}

}  // namespace

class WbGreyworldAuto final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.wb.greyworld_auto";
    static constexpr const char* VERSION = "1.0.0";

    /// Estimates gray-world neutral and emits the DNG white-balance metadata chain from
    /// docs/research/07-classic-isp-algorithms.md §3.3 and
    /// docs/research/13-color-management.md §3.6.
    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*, const sdk::ParamView&,
                              std::span<const sdk::Buffer*> inputs, std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || inputs[0] == nullptr ||
            outputs[0] == nullptr || out_metadata.empty() || out_metadata[0] == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "wb.greyworld missing buffers"});
        }

        const auto dims = checked_rgba16_image(*inputs[0], *outputs[0]);
        if (!dims) {
            return tl::unexpected(dims.error());
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "wb.greyworld missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }

        const auto pixel_count =
            static_cast<std::size_t>((*dims)[0]) * static_cast<std::size_t>((*dims)[1]);
        const auto input_lock = inputs[0]->lock_cpu(sdk::CpuAccess::Read);
        if (!input_lock) {
            return tl::unexpected(input_lock.error());
        }
        const auto output_lock = outputs[0]->lock_cpu(sdk::CpuAccess::Write);
        if (!output_lock) {
            const auto unlock = inputs[0]->unlock_cpu();
            if (!unlock) {
                return tl::unexpected(unlock.error());
            }
            return tl::unexpected(output_lock.error());
        }

        const auto* input_pixels = static_cast<const std::uint16_t*>(*input_lock);
        auto* output_pixels = static_cast<std::uint16_t*>(*output_lock);
        const auto neutral = estimate_neutral_from_pixels(input_pixels, pixel_count);
        if (!neutral) {
            const auto out_unlock = outputs[0]->unlock_cpu();
            const auto in_unlock = inputs[0]->unlock_cpu();
            if (!out_unlock) {
                return tl::unexpected(out_unlock.error());
            }
            if (!in_unlock) {
                return tl::unexpected(in_unlock.error());
            }
            return tl::unexpected(neutral.error());
        }
        apply_neutral(input_pixels, output_pixels, pixel_count, *neutral);

        const auto out_unlock = outputs[0]->unlock_cpu();
        const auto in_unlock = inputs[0]->unlock_cpu();
        if (!out_unlock) {
            return tl::unexpected(out_unlock.error());
        }
        if (!in_unlock) {
            return tl::unexpected(in_unlock.error());
        }
        if (const auto flushed = outputs[0]->flush_cpu_writes(); !flushed) {
            return tl::unexpected(flushed.error());
        }

        const auto wb_calibration = wb_detail::interpolate_calibration(*calibration, *neutral);
        if (!wb_calibration) {
            return tl::unexpected(wb_calibration.error());
        }
        if (const auto set = out_metadata[0]->set_as_shot_neutral(*neutral); !set) {
            return tl::unexpected(set.error());
        }
        if (const auto set = wb_detail::emit_wb_blobs(*out_metadata[0], *neutral, *wb_calibration);
            !set) {
            return tl::unexpected(set.error());
        }
        return out_metadata[0]->add_applied_step("white_balance");
    }
};

}  // namespace cpipe::nodes

extern const char WB_GREYWORLD_AUTO_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::WbGreyworldAuto, WB_GREYWORLD_AUTO_MANIFEST_JSON)

void cpipe_link_builtin_wb_greyworld_auto() {}
