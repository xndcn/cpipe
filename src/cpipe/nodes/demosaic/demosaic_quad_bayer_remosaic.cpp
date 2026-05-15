// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace cpipe::nodes {
namespace {

constexpr std::array<std::uint8_t, 16> kSonyQbc{0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2};

constexpr std::array<std::uint8_t, 2> kRggbRepeat{2, 2};
constexpr std::array<std::uint8_t, 16> kRggbPattern{0, 1, 1, 2};

bool is_sony_quad_bayer_cfa(const sdk::CalibrationView& calibration) {
    return calibration.has_cfa && calibration.cfa_repeat == std::array<std::uint8_t, 2>{4, 4} &&
           std::equal(kSonyQbc.begin(), kSonyQbc.end(), calibration.cfa_pattern.begin());
}

bool is_regular_bayer_cfa(const sdk::CalibrationView& calibration) {
    return calibration.has_cfa && calibration.cfa_repeat == std::array<std::uint8_t, 2>{2, 2};
}

sdk::Result<void> copy_r16_image(const sdk::Buffer& input, const sdk::Buffer& output) {
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
                            static_cast<std::size_t>((*dims)[1]) * sizeof(std::uint16_t);
    std::memcpy(*output_lock, *input_lock, byte_count);
    (void)input.unlock_cpu();
    auto unlock_status = output.unlock_cpu();
    if (!unlock_status) {
        return unlock_status;
    }
    return output.flush_cpu_writes();
}

}  // namespace

class DemosaicQuadBayerRemosaic final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.demosaic.quad_bayer_remosaic";
    static constexpr const char* VERSION = "1.0.0";

    /// Re-implements the Quad Bayer remosaic stage described in
    /// docs/research/07-classic-isp-algorithms.md §3.14; no vendor or GPL code
    /// is consulted.
    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 1 || outputs.size() != 1 || out_metadata.empty() ||
            out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "quad_bayer_remosaic missing buffers"});
        }

        const auto* metadata = inputs[0] == nullptr ? nullptr : inputs[0]->metadata();
        if (metadata == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_NEED_METADATA, "quad_bayer_remosaic missing metadata"});
        }
        const auto calibration = metadata->calibration();
        if (!calibration) {
            return tl::unexpected(calibration.error());
        }
        if (is_regular_bayer_cfa(*calibration)) {
            if (auto copied = copy_r16_image(*inputs[0], *outputs[0]); !copied) {
                return copied;
            }
            return out_metadata[0]->add_applied_step("quad_bayer_remosaic");
        }
        if (!is_sony_quad_bayer_cfa(*calibration)) {
            return tl::unexpected(
                sdk::Error{CPIPE_UNSUPPORTED, "quad_bayer_remosaic requires Sony 4x4 QBC CFA"});
        }

        const auto submitted =
            compute.submit_halide("demosaic_quad_bayer_remosaic", inputs, outputs);
        if (!submitted) {
            return tl::unexpected(submitted.error());
        }
        if (auto cfa = out_metadata[0]->set_cfa(kRggbRepeat, kRggbPattern); !cfa) {
            return cfa;
        }
        return out_metadata[0]->add_applied_step("quad_bayer_remosaic");
    }
};

}  // namespace cpipe::nodes

extern const char DEMOSAIC_QUAD_BAYER_REMOSAIC_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DemosaicQuadBayerRemosaic,
                    DEMOSAIC_QUAD_BAYER_REMOSAIC_MANIFEST_JSON)

void cpipe_link_builtin_demosaic_quad_bayer_remosaic_halide();

void cpipe_link_builtin_demosaic_quad_bayer_remosaic() {
    cpipe_link_builtin_demosaic_quad_bayer_remosaic_halide();
}
