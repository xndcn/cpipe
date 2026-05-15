// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

#include <cstdint>

namespace cpipe::nodes {
namespace {

Halide::Expr clamp01(const Halide::Expr& value) {
    return Halide::clamp(value, 0.0F, 1.0F);
}

Halide::Expr add_delta(const Halide::Expr& base, const Halide::Expr& from, const Halide::Expr& to,
                       const Halide::Expr& weight) {
    return base + ((to - from) * weight);
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

class Color3dLutGenerator final : public Halide::Generator<Color3dLutGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<Halide::Buffer<float, 2>> lut{"lut"};
    Input<std::int32_t> lut_size{"lut_size"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Applies a 3D RGB LUT with tetrahedral interpolation as selected in
    /// docs/research/07-classic-isp-algorithms.md §3.10 and P2-PD-25.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        lut.dim(0).set_bounds(0, 3).set_stride(1);
        lut.dim(1).set_stride(3);

        const Halide::Expr max_index = lut_size - 1;
        const Halide::Expr r_scaled =
            clamp01(Halide::cast<float>(input(x, y, 0))) * Halide::cast<float>(max_index);
        const Halide::Expr g_scaled =
            clamp01(Halide::cast<float>(input(x, y, 1))) * Halide::cast<float>(max_index);
        const Halide::Expr b_scaled =
            clamp01(Halide::cast<float>(input(x, y, 2))) * Halide::cast<float>(max_index);
        const Halide::Expr r0 =
            Halide::clamp(Halide::cast<std::int32_t>(Halide::floor(r_scaled)), 0, lut_size - 2);
        const Halide::Expr g0 =
            Halide::clamp(Halide::cast<std::int32_t>(Halide::floor(g_scaled)), 0, lut_size - 2);
        const Halide::Expr b0 =
            Halide::clamp(Halide::cast<std::int32_t>(Halide::floor(b_scaled)), 0, lut_size - 2);
        const Halide::Expr dr = r_scaled - Halide::cast<float>(r0);
        const Halide::Expr dg = g_scaled - Halide::cast<float>(g0);
        const Halide::Expr db = b_scaled - Halide::cast<float>(b0);
        const Halide::Expr rgb_channel = Halide::min(c, 2);

        const Halide::Expr value = tetrahedral(rgb_channel, r0, g0, b0, dr, dg, db);
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, Halide::cast<float>(input(x, y, c)), clamp01(value)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }

private:
    Halide::Expr sample(const Halide::Expr& channel, const Halide::Expr& r, const Halide::Expr& g,
                        const Halide::Expr& b) {
        return lut(channel, ((r * lut_size) + g) * lut_size + b);
    }

    Halide::Expr tetrahedral(const Halide::Expr& channel, const Halide::Expr& r0,
                             const Halide::Expr& g0, const Halide::Expr& b0, const Halide::Expr& dr,
                             const Halide::Expr& dg, const Halide::Expr& db) {
        const auto c000 = sample(channel, r0, g0, b0);
        const auto c100 = sample(channel, r0 + 1, g0, b0);
        const auto c010 = sample(channel, r0, g0 + 1, b0);
        const auto c001 = sample(channel, r0, g0, b0 + 1);
        const auto c110 = sample(channel, r0 + 1, g0 + 1, b0);
        const auto c101 = sample(channel, r0 + 1, g0, b0 + 1);
        const auto c011 = sample(channel, r0, g0 + 1, b0 + 1);
        const auto c111 = sample(channel, r0 + 1, g0 + 1, b0 + 1);

        const auto rgb_order =
            add_delta(add_delta(add_delta(c000, c000, c100, dr), c100, c110, dg), c110, c111, db);
        const auto rbg_order =
            add_delta(add_delta(add_delta(c000, c000, c100, dr), c100, c101, db), c101, c111, dg);
        const auto brg_order =
            add_delta(add_delta(add_delta(c000, c000, c001, db), c001, c101, dr), c101, c111, dg);
        const auto grb_order =
            add_delta(add_delta(add_delta(c000, c000, c010, dg), c010, c110, dr), c110, c111, db);
        const auto gbr_order =
            add_delta(add_delta(add_delta(c000, c000, c010, dg), c010, c011, db), c011, c111, dr);
        const auto bgr_order =
            add_delta(add_delta(add_delta(c000, c000, c001, db), c001, c011, dg), c011, c111, dr);

        return Halide::select(dr >= dg && dg >= db, rgb_order, dr >= db && db >= dg, rbg_order,
                              db >= dr && dr >= dg, brg_order, dg >= dr && dr >= db, grb_order,
                              dg >= db && db >= dr, gbr_order, bgr_order);
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::Color3dLutGenerator, color_3d_lut)
