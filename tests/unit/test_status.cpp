// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/Status.hpp"

TEST_CASE("Status codes have stable strings") {
    REQUIRE(cpipe::to_string(cpipe::StatusCode::Ok) == "CPIPE_OK");
    REQUIRE(cpipe::to_string(cpipe::StatusCode::Failed) == "CPIPE_FAILED");
    REQUIRE(cpipe::to_string(cpipe::StatusCode::Unsupported) == "CPIPE_UNSUPPORTED");
    REQUIRE(cpipe::to_string(cpipe::StatusCode::NotFound) == "CPIPE_NOT_FOUND");
}
