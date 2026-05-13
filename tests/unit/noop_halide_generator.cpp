// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace cpipe::tests {

class MetadataNoopGenerator final : public Halide::Generator<MetadataNoopGenerator> {
public:
    Output<Halide::Buffer<std::uint8_t, 2>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"};
        output(x, y) = Halide::cast<std::uint8_t>(0);
    }
};

}  // namespace cpipe::tests

HALIDE_REGISTER_GENERATOR(cpipe::tests::MetadataNoopGenerator, metadata_noop)
