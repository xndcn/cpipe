// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace {

using namespace Halide;  // NOLINT(google-build-using-namespace)

class TestPassthroughCopy final : public Halide::Generator<TestPassthroughCopy> {
public:
    Input<Buffer<std::uint8_t, 3>> input{"input"};
    Output<Buffer<std::uint8_t, 3>> output{"output"};

    auto generate() -> void {
        output(x_, y_, channel_) = input(x_, y_, channel_);
    }

    auto schedule() -> void {
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        output.dim(0).set_stride(4);
        output.dim(2).set_stride(1);
        output.parallel(y_);
    }

private:
    Var x_{"x"};
    Var y_{"y"};
    Var channel_{"channel"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TestPassthroughCopy, test_passthrough_copy)
