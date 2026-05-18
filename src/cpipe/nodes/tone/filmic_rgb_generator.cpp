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

Halide::Expr filmic_curve(Halide::Expr value) {
    value = Halide::max(0.0F, value);
    constexpr float kContrast = 6.0F;
    constexpr float kWhite = 18.0F;
    return Halide::clamp(
        Halide::log(1.0F + (kContrast * value)) / Halide::log(1.0F + (kContrast * kWhite)), 0.0F,
        1.0F);
}

Halide::Expr adjust_range(const Halide::Expr& value, const Halide::Expr& highlights,
                          const Halide::Expr& shadows) {
    const auto shadow_delta = (shadows - 1.0F) * 0.08F * (1.0F - value) * (1.0F - value);
    const auto highlight_delta = (highlights - 1.0F) * 0.08F * value * value;
    return Halide::clamp(value + shadow_delta + highlight_delta, 0.0F, 1.0F);
}

}  // namespace

class ToneFilmicRgbGenerator final : public Halide::Generator<ToneFilmicRgbGenerator> {
public:
    Input<Halide::Buffer<Halide::float16_t, 3>> input{"input"};
    Input<float> ev{"ev"};
    Input<float> contrast{"contrast"};
    Input<float> saturation{"saturation"};
    Input<float> highlights{"highlights"};
    Input<float> shadows{"shadows"};
    Output<Halide::Buffer<Halide::float16_t, 3>> output{"output"};

    /// Implements the fixed filmic-RGB-style global curve selected in
    /// docs/research/07-classic-isp-algorithms.md §3.4 / §4.4 from Pierre 2018 and
    /// the darktable filmic-RGB manual, without consulting GPLv3 source code.
    void generate() {
        Halide::Var x{"x"}, y{"y"}, c{"c"};
        input.dim(0).set_stride(4);
        input.dim(2).set_stride(1);
        Halide::Func value{"value"};
        value(x, y, c) = Halide::cast<float>(input(x, y, c));
        Halide::Func toned{"toned"};
        const auto exposed = value(x, y, c) * Halide::pow(2.0F, ev);
        const auto curved = filmic_curve(exposed);
        const auto contrasted = Halide::pow(Halide::max(0.0F, curved), 1.0F / contrast);
        toned(x, y, c) = adjust_range(contrasted, highlights, shadows);

        const auto luma =
            (0.2126F * toned(x, y, 0)) + (0.7152F * toned(x, y, 1)) + (0.0722F * toned(x, y, 2));
        const auto saturated =
            Halide::clamp(luma + ((toned(x, y, c) - luma) * saturation), 0.0F, 1.0F);
        output(x, y, c) =
            Halide::cast<Halide::float16_t>(Halide::select(c == 3, value(x, y, c), saturated));
        output.dim(2).set_bounds(0, 4);
    }

    void schedule() {
        schedule_rgba(output, get_target());
    }
};

}  // namespace cpipe::nodes

HALIDE_REGISTER_GENERATOR(cpipe::nodes::ToneFilmicRgbGenerator, tone_filmic_rgb)
