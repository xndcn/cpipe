// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>
#include <utility>

namespace cpipe::nodes {
namespace {

Halide::Expr clamp_unit(Halide::Expr value) {
    return Halide::clamp(std::move(value), 0.0F, 1.0F);
}

template <class OutputBuffer>
void schedule_rgba(OutputBuffer& output, const Halide::Target& target) {
    Halide::Var x{"x"}, y{"y"}, c{"c"};
    output.dim(0).set_stride(4);
    output.dim(2).set_stride(1);
    if (target.has_gpu_feature()) {
        Halide::Var xi{"xi"}, yi{"yi"};
        output.reorder(c, x, y).unroll(c).gpu_tile(x, y, xi, yi, 16, 16);
    } else {
        output.reorder(c, x, y).unroll(c).parallel(y);
    }
}

}  // namespace

class PrecisionConvertR16ToF32 final : public Halide::Generator<PrecisionConvertR16ToF32> {
public:
    Input<Halide::Buffer<std::uint16_t, 2>> input{"input"};
    Output<Halide::Buffer<float, 2>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"};
        output(x, y) = Halide::cast<float>(input(x, y));
    }

    void schedule() {
        Halide::Var x{"x"}, y{"y"};
        output.parallel(y).vectorize(x, 8);
    }
};

class PrecisionConvertF32ToRgba16f final : public Halide::Generator<PrecisionConvertF32ToRgba16f> {
public:
    Input<Halide::Buffer<float, 2>> input{"input"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        output(x, y, c) =
            Halide::cast<Halide::float16_t>(Halide::select(c == 3, 1.0F, input(x, y)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

class PrecisionConvertRgba16fToRgba8 final
    : public Halide::Generator<PrecisionConvertRgba16fToRgba8> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Output<Halide::Buffer<std::uint8_t, 3>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        const Halide::Expr scaled =
            (clamp_unit(Halide::cast<float>(input(x, y, c))) * 255.0F) + 0.5F;
        output(x, y, c) = Halide::cast<std::uint8_t>(scaled);
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

class PrecisionConvertRgba16fToRgba16u final
    : public Halide::Generator<PrecisionConvertRgba16fToRgba16u> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Output<Halide::Buffer<std::uint16_t, 3>> output{"output"};

    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        const Halide::Expr scaled =
            (clamp_unit(Halide::cast<float>(input(x, y, c))) * 65535.0F) + 0.5F;
        output(x, y, c) = Halide::cast<std::uint16_t>(scaled);
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::PrecisionConvertR16ToF32, precision_convert_r16u_to_f32)
HALIDE_REGISTER_GENERATOR(cpipe::nodes::PrecisionConvertF32ToRgba16f,
                          precision_convert_f32_to_rgba16f)
HALIDE_REGISTER_GENERATOR(cpipe::nodes::PrecisionConvertRgba16fToRgba8,
                          precision_convert_rgba16f_to_rgba8)
HALIDE_REGISTER_GENERATOR(cpipe::nodes::PrecisionConvertRgba16fToRgba16u,
                          precision_convert_rgba16f_to_rgba16u)
