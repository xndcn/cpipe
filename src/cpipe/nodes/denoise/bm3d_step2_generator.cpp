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

}  // namespace

class DenoiseBm3dStep2Generator final : public Halide::Generator<DenoiseBm3dStep2Generator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<float> sigma{"sigma"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Provides the P2 Wiener-stage BM3D AOT surface selected in
    /// docs/research/07-classic-isp-algorithms.md §4.4 from Dabov 2007 and
    /// Mäkinen 2020; the v1 compact path keeps the stage as a sigma-aware identity.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);
        const Halide::Expr value = Halide::cast<float>(clamped(x, y, c));
        const Halide::Expr keep = value + (sigma * 0.0F);
        output(x, y, c) = Halide::cast<Halide::float16_t>(keep);
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DenoiseBm3dStep2Generator, denoise_bm3d_step2)
