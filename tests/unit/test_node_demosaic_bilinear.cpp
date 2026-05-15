// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "demosaic_bilinear_fixture.hpp"

TEST_CASE("demosaic.bilinear runs Halide CPU path and updates metadata") {
    cpipe::tests::assert_demosaic_node();
}
