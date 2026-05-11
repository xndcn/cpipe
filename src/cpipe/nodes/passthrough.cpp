// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/nodes/Passthrough.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

#include "passthrough_copy.h"

extern const char PASSTHROUGH_MANIFEST_JSON[];

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

void register_passthrough_halide(runtime::ComputeContext& context) {
    context.register_halide("passthrough_copy", &passthrough_copy);
}

}  // namespace cpipe::nodes

CPIPE_REGISTER_NODE(cpipe::nodes::Passthrough, PASSTHROUGH_MANIFEST_JSON)
