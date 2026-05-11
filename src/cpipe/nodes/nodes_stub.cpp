// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace cpipe::nodes {

class RegistrySmokeNode final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.builtin.registry_smoke";
    static constexpr const char* VERSION = "0.1.0";

    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*, const sdk::ParamView&,
                              std::span<const sdk::Buffer*>, std::span<sdk::Buffer*>) override {
        return {};
    }
};

int nodes_link_anchor() {
    return 0;
}

}  // namespace cpipe::nodes

CPIPE_REGISTER_NODE(cpipe::nodes::RegistrySmokeNode,
                    R"json({"id":"com.cpipe.builtin.registry_smoke"})json")
