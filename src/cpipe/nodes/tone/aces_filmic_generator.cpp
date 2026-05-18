// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

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

Halide::Expr aces_fit(Halide::Expr value) {
    value = Halide::max(0.0F, value);
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    return Halide::clamp((value * ((a * value) + b)) / ((value * ((c * value) + d)) + e), 0.0F,
                         1.0F);
}

}  // namespace

class ToneAcesFilmicGenerator final : public Halide::Generator<ToneAcesFilmicGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<std::int32_t> enabled{"enabled"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the ACES filmic curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 using the Narkowicz 2016 fit.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(input(x, y, c));
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3 || enabled == 0, value(x, y, c), aces_fit(value(x, y, c))));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::ToneAcesFilmicGenerator, tone_aces_filmic)
