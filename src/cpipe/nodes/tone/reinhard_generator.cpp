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

Halide::Expr reinhard(const Halide::Expr& value, const Halide::Expr& white_point) {
    const auto clamped_value = Halide::max(0.0F, value);
    return Halide::clamp((clamped_value * (1.0F + (clamped_value / (white_point * white_point)))) /
                             (1.0F + clamped_value),
                         0.0F, 1.0F);
}

}  // namespace

class ToneReinhardGenerator final : public Halide::Generator<ToneReinhardGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<float> white_point{"white_point"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements Reinhard et al. 2002 global tone mapping as selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(input(x, y, c));
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, value(x, y, c), reinhard(value(x, y, c), white_point)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::ToneReinhardGenerator, tone_reinhard)
