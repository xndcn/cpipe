// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "demosaic_bilinear_fixture.hpp"

void cpipe_link_builtin_demosaic_rcd();

TEST_CASE("demosaic.rcd runs Halide CPU path for all Bayer patterns and updates metadata") {
    const auto raw = cpipe::tests::synthetic_bayer();

    for (const auto& pattern : cpipe::tests::cfa_patterns()) {
        const auto run = cpipe::tests::run_demosaic_node(
            "com.cpipe.demosaic.rcd", &cpipe_link_builtin_demosaic_rcd, pattern, raw);

        for (std::uint32_t y = 0; y < 16U; ++y) {
            for (std::uint32_t x = 0; x < 16U; ++x) {
                const auto cfa = cpipe::tests::cfa_at(pattern, static_cast<std::int32_t>(x),
                                                      static_cast<std::int32_t>(y));
                const auto base = (static_cast<std::size_t>(y) * 16U + x) * 4U;
                const auto center = raw[(static_cast<std::size_t>(y) * 16U) + x];
                if (cfa == 0U) {
                    REQUIRE(run.pixels[base + 0U] == Catch::Approx(center).margin(0.001F));
                } else if (cfa == 1U) {
                    REQUIRE(run.pixels[base + 1U] == Catch::Approx(center).margin(0.001F));
                } else {
                    REQUIRE(run.pixels[base + 2U] == Catch::Approx(center).margin(0.001F));
                }
                REQUIRE(run.pixels[base + 3U] == Catch::Approx(1.0F));
            }
        }
    }
}
