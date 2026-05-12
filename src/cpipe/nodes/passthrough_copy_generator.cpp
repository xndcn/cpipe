// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace {

class PassthroughCopy : public Halide::Generator<PassthroughCopy> {
public:
    Input<Halide::Buffer<uint32_t, 2>> input{"input"};
    Output<Halide::Buffer<uint32_t, 2>> output{"output"};

    void generate() {
        Halide::Var x{"x"};
        Halide::Var y{"y"};
        output(x, y) = input(x, y);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(PassthroughCopy, passthrough_copy)
