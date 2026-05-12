// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace {

class TestPassthroughCopy final : public Halide::Generator<TestPassthroughCopy> {
public:
    Input<Buffer<std::uint8_t, 3>> input{"input"};
    Output<Buffer<std::uint8_t, 3>> output{"output"};

    void generate() {
        Halide::Var c{"c"};
        Halide::Var x{"x"};
        Halide::Var y{"y"};

        output(c, x, y) = input(c, x, y);
        output.parallel(y);
    }
};

}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,modernize-type-traits)
HALIDE_REGISTER_GENERATOR(TestPassthroughCopy, passthrough_copy)
