// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <CLI/CLI.hpp>
#include <exception>
#include <filesystem>
#include <iostream>

#include "cpipe/runtime/Pipeline.hpp"

int main(int argc, char** argv) {
    CLI::App app{"cpipe"};

    std::filesystem::path input;
    std::filesystem::path pipeline_path;
    std::filesystem::path output;

    auto* run = app.add_subcommand("run", "Run a pipeline");
    run->add_option("input", input)->required();
    run->add_option("-p,--pipeline", pipeline_path)->required();
    run->add_option("-o,--output", output)->required();
    app.require_subcommand(1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& err) {
        return app.exit(err);
    }

    if (*run) {
        auto pipeline = cpipe::runtime::Pipeline::load(pipeline_path);
        if (!pipeline) {
            std::cerr << "cpipe: " << pipeline.error().message << '\n';
            return static_cast<int>(pipeline.error().code);
        }
        auto status = pipeline->run_file(input, output);
        if (!status) {
            std::cerr << "cpipe: " << status.error().message << '\n';
            return static_cast<int>(status.error().code);
        }
    }

    return 0;
}
