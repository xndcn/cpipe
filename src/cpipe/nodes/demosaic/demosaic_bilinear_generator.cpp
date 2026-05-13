// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {

class DemosaicBilinearGenerator final : public Halide::Generator<DemosaicBilinearGenerator> {
public:
    Input<Halide::Buffer<float, 2>> input{"input"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the trivial Bayer bilinear fallback described in
    /// docs/research/07-classic-isp-algorithms.md §3.1.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        const Halide::Expr center = clamped(x, y);
        const Halide::Expr horizontal = 0.5F * (clamped(x - 1, y) + clamped(x + 1, y));
        const Halide::Expr vertical = 0.5F * (clamped(x, y - 1) + clamped(x, y + 1));
        const Halide::Expr cross =
            0.25F * (clamped(x - 1, y) + clamped(x + 1, y) + clamped(x, y - 1) + clamped(x, y + 1));
        const Halide::Expr diagonal = 0.25F * (clamped(x - 1, y - 1) + clamped(x + 1, y - 1) +
                                               clamped(x - 1, y + 1) + clamped(x + 1, y + 1));

        const Halide::Expr red_site = ((x & 1) == 0) && ((y & 1) == 0);
        const Halide::Expr blue_site = ((x & 1) == 1) && ((y & 1) == 1);
        const Halide::Expr green_on_red_row = ((x & 1) == 1) && ((y & 1) == 0);

        const Halide::Expr r = Halide::select(red_site, center, blue_site, diagonal,
                                              green_on_red_row, horizontal, vertical);
        const Halide::Expr g = Halide::select(red_site || blue_site, cross, center);
        const Halide::Expr b = Halide::select(blue_site, center, red_site, diagonal,
                                              green_on_red_row, vertical, horizontal);
        const Halide::Expr rgba = Halide::select(c == 0, r, c == 1, g, c == 2, b, 1.0F);

        output(x, y, c) = Halide::cast<Halide::float16_t>(rgba);
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        Halide::Var x{"x"}, y{"y"}, c{"c"}, xi{"xi"}, yi{"yi"};
        output.dim(0).set_stride(4);
        output.dim(2).set_stride(1);
        if (get_target().has_gpu_feature()) {
            output.reorder(c, x, y).unroll(c).gpu_tile(x, y, xi, yi, 16, 16);
        } else {
            output.reorder(c, x, y).unroll(c).parallel(y);
        }
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DemosaicBilinearGenerator, demosaic_bilinear)
