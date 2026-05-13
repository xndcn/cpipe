// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/ingest/dng/DngReader.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <filesystem>
#include <span>
#include <string>

namespace cpipe::nodes {

class DngInput final : public sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.builtin.dng_input";
    static constexpr const char* VERSION = "1.0.0";

    sdk::Result<void> process(sdk::ComputeContext&, sdk::InferenceContext*,
                              const sdk::ParamView& params, std::span<const sdk::Buffer*>,
                              std::span<sdk::Buffer*> outputs,
                              std::span<sdk::MetadataBuilder*>) override {
        if (outputs.empty() || outputs[0] == nullptr || outputs[0]->impl() == nullptr) {
            return tl::unexpected(sdk::Error{CPIPE_BAD_INDEX, "dng_input missing output"});
        }

        const auto path = params.string("path");
        if (!path) {
            return tl::unexpected(path.error());
        }

        const auto read = ingest::dng::DngReader::read(std::filesystem::path{std::string{*path}});
        if (read.status != CPIPE_OK) {
            return tl::unexpected(sdk::Error{read.status, read.message});
        }

        outputs[0]->impl()->buffer = read.buffer;
        return {};
    }
};

}  // namespace cpipe::nodes

extern const char DNG_INPUT_MANIFEST_JSON[];

CPIPE_REGISTER_NODE(cpipe::nodes::DngInput, DNG_INPUT_MANIFEST_JSON)

void cpipe_link_builtin_dng_input() {}
