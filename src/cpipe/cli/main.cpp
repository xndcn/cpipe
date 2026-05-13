// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

void cpipe_link_all_builtin_nodes();

namespace {

bool has_dng_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".dng";
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"cpipe"};
    app.allow_extras(false);

    std::filesystem::path input;
    std::filesystem::path pipeline_path;
    std::filesystem::path output;

    auto* run = app.add_subcommand("run", "Run a pipeline");
    run->add_option("input", input, "Input DNG or RGBA8 binary")->required();
    run->add_option("-p,--pipeline", pipeline_path, "Pipeline JSON")->required();
    run->add_option("-o,--output", output, "Output HEIF or binary")->required();

    CLI11_PARSE(app, argc, argv);

    if (*run) {
        cpipe_link_all_builtin_nodes();

        cpipe::runtime::Registry registry;
        registry.load_builtin_nodes();

        cpipe::runtime::Pipeline pipeline;
        std::string error;
        auto status = cpipe::runtime::Pipeline::load(pipeline_path, registry, &pipeline, &error);
        if (status != CPIPE_OK) {
            std::cerr << error << '\n';
            return static_cast<int>(status);
        }

        if (has_dng_extension(input)) {
            status = pipeline.set_source("raw", "com.cpipe.builtin.dng_input",
                                         nlohmann::json{{"path", input.string()}});
            if (status != CPIPE_OK) {
                std::cerr << "failed to bind DNG source\n";
                return static_cast<int>(status);
            }
            status = pipeline.run_to_file(output, &error);
        } else {
            status = pipeline.run_file(input, output, &error);
        }
        if (status != CPIPE_OK) {
            std::cerr << error << '\n';
            return static_cast<int>(status);
        }
    }
    return 0;
}
