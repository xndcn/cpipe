// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {
namespace {

Halide::Expr mean2x2(const Halide::Func& value, const Halide::Expr& x, const Halide::Expr& y,
                     const Halide::Expr& c) {
    const Halide::Expr bx = (x / 2) * 2;
    const Halide::Expr by = (y / 2) * 2;
    return (value(bx, by, c) + value(bx + 1, by, c) + value(bx, by + 1, c) +
            value(bx + 1, by + 1, c)) *
           0.25F;
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

class DenoiseBm3dStep1Generator final : public Halide::Generator<DenoiseBm3dStep1Generator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<float> sigma{"sigma"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the P2 hard-threshold BM3D stage surface from
    /// docs/research/07-classic-isp-algorithms.md §4.4; the compact v1
    /// approximation uses 2x2 collaborative grouping derived from Dabov 2007.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(clamped(x, y, c));
        const Halide::Expr basic = mean2x2(value, x, y, c);
        const Halide::Expr strength = Halide::min(1.0F, Halide::max(0.0F, sigma / 0.03F));
        const Halide::Expr denoised = (value(x, y, c) * (1.0F - strength)) + (basic * strength);
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, value(x, y, c), Halide::max(0.0F, denoised)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DenoiseBm3dStep1Generator, denoise_bm3d_step1)
