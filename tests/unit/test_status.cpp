// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/Status.hpp>

using cpipe::Status;
using cpipe::status_from_code;
using cpipe::to_code;
using cpipe::to_string;

TEST_CASE("test_status: keeps stable numeric values") {
    CHECK(to_code(Status::Ok) == 0);
    CHECK(to_code(Status::Failed) == 1);
    CHECK(to_code(Status::ReplyDefault) == 2);
    CHECK(to_code(Status::OutOfMemory) == 3);
    CHECK(to_code(Status::BadPrecision) == 4);
    CHECK(to_code(Status::BadIndex) == 5);
    CHECK(to_code(Status::NeedParam) == 6);
    CHECK(to_code(Status::InternalError) == 7);
    CHECK(to_code(Status::Unsupported) == 8);
}

TEST_CASE("test_status: stringifies and round-trips") {
    for (const auto status : {Status::Ok, Status::Failed, Status::ReplyDefault, Status::OutOfMemory,
                              Status::BadPrecision, Status::BadIndex, Status::NeedParam,
                              Status::InternalError, Status::Unsupported}) {
        CHECK_FALSE(to_string(status).empty());
        CHECK(status_from_code(to_code(status)) == status);
    }
    CHECK_FALSE(status_from_code(99).has_value());
}
