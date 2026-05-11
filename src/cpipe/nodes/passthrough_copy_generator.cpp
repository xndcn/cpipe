// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace cpipe::nodes {

class PassthroughCopy final : public Halide::Generator<PassthroughCopy> {
public:
    Input<Halide::Buffer<std::uint8_t, 1>> input{"input"};
    Output<Halide::Buffer<std::uint8_t, 1>> output{"output"};

    void generate() {
        output(x_) = input(x_);
    }

    void schedule() {
        output.parallel(x_);
    }

private:
    Halide::Var x_{"x"};
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::PassthroughCopy, PassthroughCopy)
