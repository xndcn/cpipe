// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace cpipe::nodes {
namespace {

sdk::Result<std::size_t> checked_raw16_size(const sdk::Buffer& input, const sdk::Buffer& output) {
    const auto input_format = input.format();
    const auto output_format = output.format();
    if (!input_format || !output_format) {
        return tl::unexpected(!input_format ? input_format.error() : output_format.error());
    }
    if (*input_format != static_cast<int>(compute::PixelFormat::R16_UINT) ||
        *output_format != static_cast<int>(compute::PixelFormat::R16_UINT)) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_PRECISION, "hdr_plus_derivative needs R16"});
    }

    const auto input_dims = input.dims();
    const auto output_dims = output.dims();
    if (!input_dims || !output_dims) {
        return tl::unexpected(!input_dims ? input_dims.error() : output_dims.error());
    }
    if (*input_dims != *output_dims || input_dims->size() != 2 || (*input_dims)[0] == 0 ||
        (*input_dims)[1] == 0) {
        return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "hdr_plus_derivative layout mismatch"});
    }

    return static_cast<std::size_t>((*input_dims)[0]) * static_cast<std::size_t>((*input_dims)[1]) *
           static_cast<std::size_t>(compute::bytes_per_pixel(compute::PixelFormat::R16_UINT));
}

sdk::Result<void> copy_ref_frame(const sdk::Buffer& input, const sdk::Buffer& output,
                                 std::size_t bytes) {
    const auto input_lock = input.lock_cpu(sdk::CpuAccess::Read);
    if (!input_lock) {
        return tl::unexpected(input_lock.error());
    }
    const auto output_lock = output.lock_cpu(sdk::CpuAccess::Write);
    if (!output_lock) {
        const auto unlock = input.unlock_cpu();
        if (!unlock) {
            return tl::unexpected(unlock.error());
        }
        return tl::unexpected(output_lock.error());
    }

    std::memcpy(*output_lock, *input_lock, bytes);
    const auto output_unlock = output.unlock_cpu();
    const auto input_unlock = input.unlock_cpu();
    if (!output_unlock) {
        return tl::unexpected(output_unlock.error());
    }
    if (!input_unlock) {
        return tl::unexpected(input_unlock.error());
    }
    return output.flush_cpu_writes();
}

}  // namespace

class HdrPlusDerivative final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.fusion.hdr_plus_derivative";
    static constexpr const char* VERSION = "1.0.0";

    /// P2-PD-31 placeholder: return the reference frame until P4 burst fusion lands.
    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*, const sdk::ParamView&,
                              std::span<const sdk::Buffer*> inputs, std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*> out_metadata) override {
        if (inputs.size() != 2 || outputs.size() != 1 || inputs[0] == nullptr ||
            inputs[1] == nullptr || outputs[0] == nullptr || out_metadata.empty() ||
            out_metadata[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_BAD_INDEX, "hdr_plus_derivative missing buffers"});
        }

        const auto bytes = checked_raw16_size(*inputs[0], *outputs[0]);
        if (!bytes) {
            return tl::unexpected(bytes.error());
        }
        const auto copied = copy_ref_frame(*inputs[0], *outputs[0], *bytes);
        if (!copied) {
            return tl::unexpected(copied.error());
        }
        return out_metadata[0]->add_applied_step("burst_fusion_stub");
    }
};

}  // namespace cpipe::nodes

extern const char FUSION_HDR_PLUS_DERIVATIVE_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::HdrPlusDerivative, FUSION_HDR_PLUS_DERIVATIVE_MANIFEST_JSON)

void cpipe_link_builtin_fusion_hdr_plus_derivative() {}
