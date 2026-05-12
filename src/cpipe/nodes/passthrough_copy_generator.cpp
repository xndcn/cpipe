// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace {

using namespace Halide;  // NOLINT(google-build-using-namespace)

class PassthroughCopy final : public Halide::Generator<PassthroughCopy> {
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
    Var x_{"x"};  // NOLINT(readability-identifier-length)
    Var y_{"y"};  // NOLINT(readability-identifier-length)
    Var channel_{"channel"};
};

}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
HALIDE_REGISTER_GENERATOR(PassthroughCopy, passthrough_copy)
