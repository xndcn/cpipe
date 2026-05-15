// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {

class DemosaicAmazeGenerator final : public Halide::Generator<DemosaicAmazeGenerator> {
public:
    Input<Halide::Buffer<float, 2>> input{"input"};
    Input<Halide::Buffer<int32_t, 1>> cfa_pattern{"cfa_pattern"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Re-implements the AMaZE-style green correction and chroma-residual
    /// interpolation described in docs/research/07-classic-isp-algorithms.md
    /// §3.1 from Emil Martinec's public algorithm notes.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);
        const auto cfa_at = [&](const Halide::Expr& sx, const Halide::Expr& sy) {
            return cfa_pattern(((sy & 1) * 2) + (sx & 1));
        };

        const Halide::Expr center = clamped(x, y);
        const Halide::Expr site = cfa_at(x, y);
        const Halide::Expr red_site = site == 0;
        const Halide::Expr green_site = site == 1;
        const Halide::Expr blue_site = site == 2;
        const Halide::Expr center_twice = 2.0F * center;
        const Halide::Expr gh_neighbors = clamped(x - 1, y) + clamped(x + 1, y);
        const Halide::Expr gv_neighbors = clamped(x, y - 1) + clamped(x, y + 1);
        const Halide::Expr gh_second = center_twice - clamped(x - 2, y) - clamped(x + 2, y);
        const Halide::Expr gv_second = center_twice - clamped(x, y - 2) - clamped(x, y + 2);
        const Halide::Expr gh = (0.5F * gh_neighbors) + (0.25F * gh_second);
        const Halide::Expr gv = (0.5F * gv_neighbors) + (0.25F * gv_second);
        const Halide::Expr grad_h = Halide::abs(clamped(x - 1, y) - clamped(x + 1, y)) +
                                    Halide::abs(clamped(x - 2, y) - center) +
                                    Halide::abs(clamped(x + 2, y) - center);
        const Halide::Expr grad_v = Halide::abs(clamped(x, y - 1) - clamped(x, y + 1)) +
                                    Halide::abs(clamped(x, y - 2) - center) +
                                    Halide::abs(clamped(x, y + 2) - center);

        Halide::Func green{"green"};
        const Halide::Expr green_blend = 0.5F * (gh + gv);
        green(x, y) = Halide::select(green_site, center, grad_h < grad_v, gh, grad_v < grad_h, gv,
                                     green_blend);

        Halide::Func chroma{"chroma"};
        chroma(x, y) = clamped(x, y) - green(x, y);
        const Halide::Expr chroma_h = 0.5F * (chroma(x - 1, y) + chroma(x + 1, y));
        const Halide::Expr chroma_v = 0.5F * (chroma(x, y - 1) + chroma(x, y + 1));
        const Halide::Expr chroma_d = 0.25F * (chroma(x - 1, y - 1) + chroma(x + 1, y - 1) +
                                               chroma(x - 1, y + 1) + chroma(x + 1, y + 1));

        const Halide::Expr horizontal_red = cfa_at(x - 1, y) == 0;
        const Halide::Expr horizontal_blue = cfa_at(x - 1, y) == 2;
        const Halide::Expr red_h = green(x, y) + chroma_h;
        const Halide::Expr red_v = green(x, y) + chroma_v;
        const Halide::Expr red_d = green(x, y) + chroma_d;
        const Halide::Expr red_interp =
            Halide::select(blue_site, red_d, horizontal_red, red_h, red_v);
        const Halide::Expr r = Halide::max(0.0F, Halide::select(red_site, center, red_interp));
        const Halide::Expr g = Halide::max(0.0F, green(x, y));
        const Halide::Expr blue_interp =
            Halide::select(red_site, red_d, horizontal_blue, red_h, red_v);
        const Halide::Expr b = Halide::max(0.0F, Halide::select(blue_site, center, blue_interp));
        const Halide::Expr rgba = Halide::select(c == 0, r, c == 1, g, c == 2, b, 1.0F);

        output(x, y, c) = Halide::cast<Halide::float16_t>(rgba);
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        output.dim(0).set_stride(4);
        output.dim(2).set_stride(1);
        output.reorder(c, x, y).unroll(c).parallel(y);
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DemosaicAmazeGenerator, demosaic_amaze)
