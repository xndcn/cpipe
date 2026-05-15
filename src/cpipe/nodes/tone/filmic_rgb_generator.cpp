// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {
namespace {

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

Halide::Expr filmic_curve(Halide::Expr value) {
    value = Halide::max(0.0F, value);
    constexpr float kContrast = 6.0F;
    constexpr float kWhite = 18.0F;
    return Halide::clamp(
        Halide::log(1.0F + (kContrast * value)) / Halide::log(1.0F + (kContrast * kWhite)), 0.0F,
        1.0F);
}

}  // namespace

class ToneFilmicRgbGenerator final : public Halide::Generator<ToneFilmicRgbGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the fixed filmic-RGB-style global curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 / §4.4 from Pierre 2018 and
    /// the darktable filmic-RGB manual, without consulting GPLv3 source code.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(input(x, y, c));
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, value(x, y, c), filmic_curve(value(x, y, c))));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::ToneFilmicRgbGenerator, tone_filmic_rgb)
