// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "demosaic_bilinear_fixture.hpp"

extern "C" int demosaic_bilinear(halide_buffer_t* input, halide_buffer_t* output);

TEST_CASE("demosaic.bilinear runs Halide CPU path and updates metadata") {
    cpipe::tests::assert_demosaic_node(&demosaic_bilinear);
}
