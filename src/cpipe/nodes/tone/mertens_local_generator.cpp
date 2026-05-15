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

Halide::Expr clamp_sdr(const Halide::Expr& value) {
    return Halide::clamp(value, 0.0F, 1.0F);
}

Halide::Expr channel_exposure_weight(const Halide::Expr& value) {
    constexpr float sigma = 0.20F;
    const auto distance = value - 0.5F;
    return Halide::exp(-(distance * distance) / (2.0F * sigma * sigma));
}

Halide::Expr exposure_weight(Halide::Func& input, const Halide::Var& x, const Halide::Var& y) {
    const auto red = Halide::max(0.0F, input(x, y, 0));
    const auto green = Halide::max(0.0F, input(x, y, 1));
    const auto blue = Halide::max(0.0F, input(x, y, 2));
    const auto mean = (red + green + blue) / 3.0F;
    const auto saturation =
        Halide::sqrt(((red - mean) * (red - mean) + (green - mean) * (green - mean) +
                      (blue - mean) * (blue - mean)) /
                         3.0F +
                     0.0001F);
    const auto well_exposed = channel_exposure_weight(red) * channel_exposure_weight(green) *
                              channel_exposure_weight(blue);
    return (saturation + 0.01F) * well_exposed;
}

}  // namespace

class ToneMertensLocalGenerator final : public Halide::Generator<ToneMertensLocalGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> under{"under"};
    Input<Halide::Buffer<Halide::float16_t, 3>> normal{"normal"};
    Input<Halide::Buffer<Halide::float16_t, 3>> over{"over"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the P2 SDR exposure-fusion weight blend selected in
    /// docs/research/07-classic-isp-algorithms.md §3.5 from Mertens 2007 and
    /// IPOL 2018; the HDR pyramid schedule remains v1.1 scope.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        under.dim(0).set_stride(4);
        under.dim(2).set_stride(1);
        normal.dim(0).set_stride(4);
        normal.dim(2).set_stride(1);
        over.dim(0).set_stride(4);
        over.dim(2).set_stride(1);

        Halide::Func under_f{"under_f"};
        Halide::Func normal_f{"normal_f"};
        Halide::Func over_f{"over_f"};
        under_f(x, y, c) = Halide::cast<float>(under(x, y, c));
        normal_f(x, y, c) = Halide::cast<float>(normal(x, y, c));
        over_f(x, y, c) = Halide::cast<float>(over(x, y, c));

        Halide::Func under_weight{"under_weight"};
        Halide::Func normal_weight{"normal_weight"};
        Halide::Func over_weight{"over_weight"};
        under_weight(x, y) = exposure_weight(under_f, x, y);
        normal_weight(x, y) = exposure_weight(normal_f, x, y);
        over_weight(x, y) = exposure_weight(over_f, x, y);

        const auto total_weight =
            Halide::max(under_weight(x, y) + normal_weight(x, y) + over_weight(x, y), 0.000001F);
        const auto fused =
            ((under_weight(x, y) * under_f(x, y, c)) + (normal_weight(x, y) * normal_f(x, y, c)) +
             (over_weight(x, y) * over_f(x, y, c))) /
            total_weight;
        output(x, y, c) = Halide::cast<Halide::float16_t>(
            Halide::select(c == 3, normal_f(x, y, c), clamp_sdr(fused)));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::ToneMertensLocalGenerator, tone_mertens_local)
