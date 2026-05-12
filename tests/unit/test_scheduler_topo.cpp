// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <vector>

#include "cpipe/runtime/Pipeline.hpp"
#include "cpipe/runtime/Scheduler.hpp"

TEST_CASE("Scheduler walks provided topological order serially") {
    std::vector<int> order;
    cpipe::runtime::Scheduler scheduler;
    const std::vector<cpipe::runtime::ScheduleStep> steps{
        {"a",
         [&]() {
             order.push_back(1);
             return cpipe::StatusCode::Ok;
         }},
        {"b",
         [&]() {
             order.push_back(2);
             return cpipe::StatusCode::Ok;
         }},
    };

    auto status = scheduler.run(steps);
    REQUIRE(status.has_value());
    REQUIRE(order == std::vector<int>{1, 2});
}

TEST_CASE("Pipeline load rejects cycles") {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_cycle_pipeline.json";
    std::ofstream out(path);
    out << R"json({
  "$schema": "https://schemas.cpipe.dev/pipeline/v0.1.json",
  "version": "0.1",
  "id": "cycle",
  "input_layout": {"kind": "Image2D", "format": "R8G8B8A8_UNORM", "dims": [16, 16]},
  "nodes": [
    {"id": "a", "type": "com.cpipe.builtin.passthrough", "params": {}},
    {"id": "b", "type": "com.cpipe.builtin.passthrough", "params": {}}
  ],
  "edges": [
    {"from": "$input", "to": "a"},
    {"from": "a", "to": "b"},
    {"from": "b", "to": "a"},
    {"from": "b", "to": "$output"}
  ]
})json";
    out.close();

    auto pipeline = cpipe::runtime::Pipeline::load(path);
    REQUIRE_FALSE(pipeline.has_value());
    REQUIRE(pipeline.error().code == cpipe::StatusCode::InvalidArgument);
}
