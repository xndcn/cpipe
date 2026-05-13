// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/Status.hpp>

TEST_CASE("Status codes stringify") {
    using cpipe::compute::StatusCode;

    REQUIRE(cpipe::compute::to_string(StatusCode::Ok) == "CPIPE_OK");
    REQUIRE(cpipe::compute::to_string(StatusCode::Failed) == "CPIPE_FAILED");
    REQUIRE(cpipe::compute::to_string(StatusCode::Unsupported) == "CPIPE_UNSUPPORTED");
}

TEST_CASE("Status ok predicate matches ABI success") {
    using cpipe::compute::StatusCode;

    REQUIRE(cpipe::compute::is_ok(StatusCode::Ok));
    REQUIRE_FALSE(cpipe::compute::is_ok(StatusCode::Failed));
}
