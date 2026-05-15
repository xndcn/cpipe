// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {
namespace {

Halide::Expr mean3x3(const Halide::Func& value, const Halide::Expr& x, const Halide::Expr& y,
                     const Halide::Expr& c) {
    return (value(x - 1, y - 1, c) + value(x, y - 1, c) + value(x + 1, y - 1, c) +
            value(x - 1, y, c) + value(x, y, c) + value(x + 1, y, c) + value(x - 1, y + 1, c) +
            value(x, y + 1, c) + value(x + 1, y + 1, c)) /
           9.0F;
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

class DenoiseGuidedFilterGenerator final : public Halide::Generator<DenoiseGuidedFilterGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the local-linear guided filter from He et al. 2010, as selected in
    /// docs/research/07-classic-isp-algorithms.md §4.3 for fast preview denoise.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(clamped(x, y, c));
        Halide::Func square{"square"};
        square(x, y, c) = value(x, y, c) * value(x, y, c);

        Halide::Func mean{"mean"};
        mean(x, y, c) = mean3x3(value, x, y, c);
        Halide::Func corr{"corr"};
        corr(x, y, c) = mean3x3(square, x, y, c);
        const Halide::Expr variance =
            Halide::max(0.0F, corr(x, y, c) - (mean(x, y, c) * mean(x, y, c)));

        constexpr float kEpsilon = 0.015F;
        Halide::Func a{"guided_a"};
        a(x, y, c) = variance / (variance + kEpsilon);
        Halide::Func b{"guided_b"};
        b(x, y, c) = mean(x, y, c) - (a(x, y, c) * mean(x, y, c));

        const Halide::Expr filtered = (mean3x3(a, x, y, c) * value(x, y, c)) + mean3x3(b, x, y, c);
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, value(x, y, c), Halide::max(0.0F, filtered)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DenoiseGuidedFilterGenerator, denoise_guided_filter)
