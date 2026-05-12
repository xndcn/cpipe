// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/Status.hpp"

TEST_CASE("StatusCode values stringify") {
    using cpipe::compute::StatusCode;

    CHECK(cpipe::compute::to_string(StatusCode::Ok) == "OK");
    CHECK(cpipe::compute::to_string(StatusCode::Failed) == "FAILED");
    CHECK(cpipe::compute::to_string(StatusCode::ReplyDefault) == "REPLY_DEFAULT");
    CHECK(cpipe::compute::to_string(StatusCode::OutOfMemory) == "OOM");
    CHECK(cpipe::compute::to_string(StatusCode::BadPrecision) == "BAD_PRECISION");
    CHECK(cpipe::compute::to_string(StatusCode::BadIndex) == "BAD_INDEX");
    CHECK(cpipe::compute::to_string(StatusCode::NeedParam) == "NEED_PARAM");
    CHECK(cpipe::compute::to_string(StatusCode::InternalError) == "INTERNAL_ERROR");
    CHECK(cpipe::compute::to_string(StatusCode::Unsupported) == "UNSUPPORTED");
}
