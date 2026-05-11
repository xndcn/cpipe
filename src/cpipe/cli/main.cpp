// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <CLI/CLI.hpp>
#include <cpipe/nodes/Passthrough.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    CLI::App app{"cpipe"};

    std::filesystem::path input;
    std::filesystem::path pipeline_path;
    std::filesystem::path output;
    bool run_requested = false;

    auto* run = app.add_subcommand("run", "Run a cpipe pipeline");
    run->add_option("input", input)->required();
    run->add_option("-p,--pipeline", pipeline_path)->required();
    run->add_option("-o,--output", output)->required();
    run->callback([&] { run_requested = true; });
    app.require_subcommand(1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& ex) {
        return app.exit(ex);
    }

    if (run_requested) {
        std::string error;
        auto pipeline = cpipe::runtime::Pipeline::load_file(pipeline_path, &error);
        if (!pipeline.has_value()) {
            std::cerr << error << '\n';
            return 1;
        }

        cpipe::runtime::ComputeContext compute;
        cpipe::nodes::register_passthrough_halide(compute);
        const auto status = pipeline->run_file(input, output, compute, &error);
        if (status != CPIPE_OK) {
            std::cerr << error << '\n';
            return 1;
        }
    }

    return 0;
}
