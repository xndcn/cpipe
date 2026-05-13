// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace cpipe::nodes {

class PassthroughCopyGenerator final : public Halide::Generator<PassthroughCopyGenerator> {
public:
    Input<Halide::Buffer<std::uint8_t, 3>> input{"input"};
    Output<Halide::Buffer<std::uint8_t, 3>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        output.dim(0).set_stride(4);
        output.dim(2).set_stride(1);
        output(x, y, c) = input(x, y, c);
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::PassthroughCopyGenerator, passthrough_copy)
