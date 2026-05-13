// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>

namespace cpipe::nodes {

class Passthrough final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.builtin.passthrough";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext& compute, sdk::InferenceContext*,
                              const sdk::ParamView&, std::span<const sdk::Buffer*> inputs,
                              std::span<sdk::Buffer*> outputs) override {
        return compute.submit_halide("passthrough_copy", inputs, outputs);
    }
};

}  // namespace cpipe::nodes

extern const char PASSTHROUGH_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::Passthrough, PASSTHROUGH_MANIFEST_JSON)

void cpipe_link_builtin_passthrough() {}
