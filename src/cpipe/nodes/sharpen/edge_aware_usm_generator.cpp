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

class SharpenEdgeAwareUsmGenerator final : public Halide::Generator<SharpenEdgeAwareUsmGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements edge-aware USM using a guided-filter-style local blur from
    /// docs/research/07-classic-isp-algorithms.md §3.7 and He et al. 2010/2013.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(clamped(x, y, c));
        const Halide::Expr blur = mean3x3(value, x, y, c);
        const Halide::Expr sharpened = value(x, y, c) + (0.75F * (value(x, y, c) - blur));
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, value(x, y, c), Halide::max(0.0F, sharpened)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::SharpenEdgeAwareUsmGenerator, sharpen_edge_aware_usm)
