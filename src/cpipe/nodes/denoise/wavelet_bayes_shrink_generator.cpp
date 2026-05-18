// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <Halide.h>

namespace cpipe::nodes {
namespace {

Halide::Expr soft_threshold(const Halide::Expr& value, const Halide::Expr& threshold) {
    return Halide::select(value > threshold, value - threshold, value < -threshold,
                          value + threshold, 0.0F);
}

Halide::Expr block_value(const Halide::Func& chroma, const Halide::Expr& x, const Halide::Expr& y,
                         const Halide::Expr& ox, const Halide::Expr& oy,
                         const Halide::Expr& threshold) {
    const Halide::Expr bx = (x / 2) * 2;
    const Halide::Expr by = (y / 2) * 2;
    const Halide::Expr p00 = chroma(bx, by);
    const Halide::Expr p10 = chroma(bx + 1, by);
    const Halide::Expr p01 = chroma(bx, by + 1);
    const Halide::Expr p11 = chroma(bx + 1, by + 1);

    const Halide::Expr ll = 0.25F * (p00 + p10 + p01 + p11);
    const Halide::Expr h = soft_threshold(0.25F * (p00 - p10 + p01 - p11), threshold);
    const Halide::Expr v = soft_threshold(0.25F * (p00 + p10 - p01 - p11), threshold);
    const Halide::Expr d = soft_threshold(0.25F * (p00 - p10 - p01 + p11), threshold);

    return Halide::select(ox == 0 && oy == 0, ll + h + v + d, ox == 1 && oy == 0, ll - h + v - d,
                          ox == 0 && oy == 1, ll + h - v - d, ll - h - v + d);
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

class DenoiseWaveletBayesShrinkGenerator final
    : public Halide::Generator<DenoiseWaveletBayesShrinkGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<float> chroma_strength{"chroma_strength"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Applies a single-level Haar chroma shrink step following the BayesShrink
    /// choice in docs/research/07-classic-isp-algorithms.md §4.3 and Chang et al. 2000.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        const Halide::Expr r = Halide::cast<float>(clamped(x, y, 0));
        const Halide::Expr g = Halide::cast<float>(clamped(x, y, 1));
        const Halide::Expr b = Halide::cast<float>(clamped(x, y, 2));
        Halide::Func luma{"luma"};
        luma(x, y) = (r + (2.0F * g) + b) * 0.25F;
        Halide::Func co{"co"};
        co(x, y) = r - b;
        Halide::Func cg{"cg"};
        cg(x, y) = g - ((r + b) * 0.5F);

        const Halide::Expr ox = x & 1;
        const Halide::Expr oy = y & 1;
        const Halide::Expr threshold = 0.035F * chroma_strength;
        const Halide::Expr denoised_co = block_value(co, x, y, ox, oy, threshold);
        const Halide::Expr denoised_cg = block_value(cg, x, y, ox, oy, threshold);
        const Halide::Expr t = luma(x, y) - (0.5F * denoised_cg);
        const Halide::Expr out_r = t + (0.5F * denoised_co);
        const Halide::Expr out_g = t + denoised_cg;
        const Halide::Expr out_b = t - (0.5F * denoised_co);
        const Halide::Expr rgb = Halide::select(c == 0, out_r, c == 1, out_g, c == 2, out_b,
                                                Halide::cast<float>(clamped(x, y, 3)));
        output(x, y, c) = Halide::cast<Halide::float16_t>(Halide::max(0.0F, rgb));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::DenoiseWaveletBayesShrinkGenerator,
                          denoise_wavelet_bayes_shrink)
