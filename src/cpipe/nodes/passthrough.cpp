// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
extern const char PASSTHROUGH_MANIFEST_JSON[];

namespace cpipe::nodes {
namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::PixelFormat;

class Passthrough final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.builtin.passthrough";
    static constexpr const char* VERSION = "1.0.0";

    [[nodiscard]] auto process(sdk::ComputeContext& compute, sdk::InferenceContext* inference,
                               const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                               std::span<sdk::Buffer*> outputs) -> sdk::Result<void> override {
        (void)inference;
        (void)params;
        if (inputs.size() != 1U || outputs.size() != 1U || inputs[0] == nullptr ||
            outputs[0] == nullptr) {
            return tl::unexpected(
                sdk::Error{CPIPE_INTERNAL_ERROR, "passthrough expects one input and one output"});
        }
        if (!is_supported(*inputs[0]) || !is_supported(*outputs[0])) {
            return tl::unexpected(
                sdk::Error{CPIPE_UNSUPPORTED, "passthrough supports RGBA8 Image2D buffers only"});
        }
        if (inputs[0]->width() != outputs[0]->width() ||
            inputs[0]->height() != outputs[0]->height()) {
            return tl::unexpected(
                sdk::Error{CPIPE_FAILED, "passthrough input and output dimensions differ"});
        }
        return compute.submit_halide("passthrough_copy", inputs, outputs);
    }

private:
    [[nodiscard]] static auto is_supported(const sdk::Buffer& buffer) noexcept -> bool {
        return buffer.kind() == static_cast<int>(BufferKind::Image2D) &&
               buffer.format() == static_cast<int>(PixelFormat::R8G8B8A8_UNORM);
    }
};

CPIPE_REGISTER_NODE(Passthrough, PASSTHROUGH_MANIFEST_JSON)

}  // namespace
}  // namespace cpipe::nodes
