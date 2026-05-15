// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdlib>

namespace cpipe::nodes {

class DemosaicQuadBayerRemosaicGenerator final
    : public Halide::Generator<DemosaicQuadBayerRemosaicGenerator> {
public:
    Input<Halide::Buffer<uint16_t, 2>> input{"input"};
    Output<Halide::Buffer<uint16_t, 2>> output{"output"};

    /// Re-implements the 4x4 Quad Bayer to 2x2 Bayer remosaic stage sketched in
    /// docs/research/07-classic-isp-algorithms.md §3.14 for Sony-style QBC.
    void generate() {
        Halide::Var x{"x"}, y{"y"};
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);
        const Halide::Expr min_x = input.dim(0).min();
        const Halide::Expr min_y = input.dim(1).min();
        const Halide::Expr max_x = min_x + input.dim(0).extent() - 1;
        const Halide::Expr max_y = min_y + input.dim(1).extent() - 1;

        const auto coord = [](const Halide::Expr& v) { return ((v % 4) + 4) % 4; };
        const auto qbc_color = [&](const Halide::Expr& sx, const Halide::Expr& sy) {
            const Halide::Expr mx = coord(sx);
            const Halide::Expr my = coord(sy);
            return Halide::select(my < 2 && mx < 2, 0, my >= 2 && mx >= 2, 2, 1);
        };
        const Halide::Expr target =
            Halide::select((y & 1) == 0 && (x & 1) == 0, 0, (y & 1) != 0 && (x & 1) != 0, 2, 1);

        Halide::Expr weighted_sum = Halide::cast<uint32_t>(0);
        Halide::Expr weight_sum = Halide::cast<uint32_t>(0);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const Halide::Expr sx = Halide::clamp(x + dx, min_x, max_x);
                const Halide::Expr sy = Halide::clamp(y + dy, min_y, max_y);
                const Halide::Expr grad_x = Halide::abs(Halide::cast<int32_t>(clamped(sx - 1, sy)) -
                                                        Halide::cast<int32_t>(clamped(sx + 1, sy)));
                const Halide::Expr grad_y = Halide::abs(Halide::cast<int32_t>(clamped(sx, sy - 1)) -
                                                        Halide::cast<int32_t>(clamped(sx, sy + 1)));
                const Halide::Expr penalty =
                    64 + grad_x + grad_y + ((std::abs(dx) + std::abs(dy)) * 16);
                const Halide::Expr candidate_weight =
                    Halide::max(Halide::cast<int32_t>(1), 65536 / penalty);
                const Halide::Expr weight = Halide::cast<uint32_t>(
                    Halide::select(qbc_color(sx, sy) == target, candidate_weight, 0));
                weighted_sum += weight * Halide::cast<uint32_t>(clamped(sx, sy));
                weight_sum += weight;
            }
        }

        output(x, y) =
            Halide::select(weight_sum == 0, Halide::cast<uint16_t>(0),
                           Halide::cast<uint16_t>((weighted_sum + (weight_sum / 2)) / weight_sum));
    }

    void schedule() {
        Halide::Var x{"x"}, y{"y"};
        output.parallel(y).vectorize(x, 8);
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DemosaicQuadBayerRemosaicGenerator,
                          demosaic_quad_bayer_remosaic)
