// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {

class DemosaicRcdGenerator final : public Halide::Generator<DemosaicRcdGenerator> {
public:
    Input<Halide::Buffer<float, 2>> input{"input"};
    Input<Halide::Buffer<int32_t, 1>> cfa_pattern{"cfa_pattern"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Re-implements the RCD skeleton in docs/research/07-classic-isp-algorithms.md
    /// §4.1 from Sanz Rodríguez & Bayón 2014.
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
        const Halide::Expr gh = 0.5F * (clamped(x - 1, y) + clamped(x + 1, y));
        const Halide::Expr gv = 0.5F * (clamped(x, y - 1) + clamped(x, y + 1));
        const Halide::Expr dh = Halide::abs(clamped(x - 2, y) - clamped(x + 2, y));
        const Halide::Expr dv = Halide::abs(clamped(x, y - 2) - clamped(x, y + 2));

        Halide::Func green{"green"};
        green(x, y) =
            Halide::select(green_site, center, dh < dv, gh, dv < dh, gv, 0.5F * (gh + gv));

        const auto safe_ratio = [](const Halide::Expr& value, const Halide::Expr& green_value) {
            return value / Halide::max(green_value, 0.000001F);
        };
        const Halide::Expr red_horizontal = green(x, y) * 0.5F *
                                            (safe_ratio(clamped(x - 1, y), green(x - 1, y)) +
                                             safe_ratio(clamped(x + 1, y), green(x + 1, y)));
        const Halide::Expr red_vertical = green(x, y) * 0.5F *
                                          (safe_ratio(clamped(x, y - 1), green(x, y - 1)) +
                                           safe_ratio(clamped(x, y + 1), green(x, y + 1)));
        const Halide::Expr red_diagonal = green(x, y) * 0.25F *
                                          (safe_ratio(clamped(x - 1, y - 1), green(x - 1, y - 1)) +
                                           safe_ratio(clamped(x + 1, y - 1), green(x + 1, y - 1)) +
                                           safe_ratio(clamped(x - 1, y + 1), green(x - 1, y + 1)) +
                                           safe_ratio(clamped(x + 1, y + 1), green(x + 1, y + 1)));

        const Halide::Expr horizontal_red = cfa_at(x - 1, y) == 0;
        const Halide::Expr horizontal_blue = cfa_at(x - 1, y) == 2;
        const Halide::Expr r =
            Halide::max(0.0F, Halide::select(red_site, center, blue_site, red_diagonal,
                                             horizontal_red, red_horizontal, red_vertical));
        const Halide::Expr g = Halide::max(0.0F, green(x, y));
        const Halide::Expr b =
            Halide::max(0.0F, Halide::select(blue_site, center, red_site, red_diagonal,
                                             horizontal_blue, red_horizontal, red_vertical));
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

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DemosaicRcdGenerator, demosaic_rcd)
