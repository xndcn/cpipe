// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/sdk/registry.hpp"

namespace cpipe::nodes {

extern const char* const CPIPE_PASSTHROUGH_MANIFEST_JSON;

class Passthrough final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.builtin.passthrough";
    static constexpr const char* VERSION = "1.0.0";

    auto process(sdk::ComputeContext& compute, sdk::InferenceContext* inference,
                 const sdk::ParamView& params, std::span<const sdk::Buffer*> inputs,
                 std::span<sdk::Buffer*> outputs) -> sdk::Result<void> override {
        (void)inference;
        (void)params;
        return compute.submit_halide("passthrough_copy", inputs, outputs);
    }
};

}  // namespace cpipe::nodes

CPIPE_REGISTER_NODE(cpipe::nodes::Passthrough, cpipe::nodes::CPIPE_PASSTHROUGH_MANIFEST_JSON)
