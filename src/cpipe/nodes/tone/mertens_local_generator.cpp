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

Halide::Expr exposure_weight(Halide::Func& input, Halide::Func& luma, const Halide::Var& x,
                             const Halide::Var& y, const Halide::Expr& weight_contrast,
                             const Halide::Expr& weight_saturation,
                             const Halide::Expr& weight_well_exposedness) {
    const auto red = Halide::max(0.0F, input(x, y, 0));
    const auto green = Halide::max(0.0F, input(x, y, 1));
    const auto blue = Halide::max(0.0F, input(x, y, 2));
    const auto mean = luma(x, y);
    const auto saturation =
        Halide::sqrt(((red - mean) * (red - mean) + (green - mean) * (green - mean) +
                      (blue - mean) * (blue - mean)) /
                         3.0F +
                     0.0001F);
    const auto well_exposed = channel_exposure_weight(red) * channel_exposure_weight(green) *
                              channel_exposure_weight(blue);
    const auto contrast = Halide::abs(luma(x, y) - 0.5F);
    const auto contrast_factor = Halide::select(weight_contrast == 0.0F, 1.0F,
                                                Halide::pow(contrast + 0.01F, weight_contrast));
    const auto saturation_factor =
        Halide::select(weight_saturation == 1.0F, saturation + 0.01F,
                       Halide::pow(saturation + 0.01F, weight_saturation));
    const auto exposure_factor =
        Halide::select(weight_well_exposedness == 1.0F, well_exposed,
                       Halide::pow(well_exposed + 0.0001F, weight_well_exposedness));
    return contrast_factor * saturation_factor * exposure_factor;
}

}  // namespace

class ToneMertensLocalGenerator final : public Halide::Generator<ToneMertensLocalGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> under{"under"};
    Input<Halide::Buffer<Halide::float16_t, 3>> normal{"normal"};
    Input<Halide::Buffer<Halide::float16_t, 3>> over{"over"};
    Input<float> weight_contrast{"weight_contrast"};
    Input<float> weight_saturation{"weight_saturation"};
    Input<float> weight_well_exposedness{"weight_well_exposedness"};
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

        Halide::Func under_luma{"under_luma"};
        Halide::Func normal_luma{"normal_luma"};
        Halide::Func over_luma{"over_luma"};
        under_luma(x, y) = (under_f(x, y, 0) + under_f(x, y, 1) + under_f(x, y, 2)) / 3.0F;
        normal_luma(x, y) = (normal_f(x, y, 0) + normal_f(x, y, 1) + normal_f(x, y, 2)) / 3.0F;
        over_luma(x, y) = (over_f(x, y, 0) + over_f(x, y, 1) + over_f(x, y, 2)) / 3.0F;

        Halide::Func under_weight{"under_weight"};
        Halide::Func normal_weight{"normal_weight"};
        Halide::Func over_weight{"over_weight"};
        under_weight(x, y) = exposure_weight(under_f, under_luma, x, y, weight_contrast,
                                             weight_saturation, weight_well_exposedness);
        normal_weight(x, y) = exposure_weight(normal_f, normal_luma, x, y, weight_contrast,
                                              weight_saturation, weight_well_exposedness);
        over_weight(x, y) = exposure_weight(over_f, over_luma, x, y, weight_contrast,
                                            weight_saturation, weight_well_exposedness);

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
