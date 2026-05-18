// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>

void cpipe_link_all_builtin_nodes();

namespace {

constexpr int kNotImplementedExit = 100;
std::atomic_bool g_stop_server{false};

void request_server_stop(int /*signal*/) {
    g_stop_server.store(true);
}

bool has_dng_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".dng";
}

int not_implemented(std::string_view command) {
    std::cerr << "cpipe " << command << " is not implemented in Phase 3.A\n";
    return kNotImplementedExit;
}

std::uint16_t parse_port(const std::string& value, std::uint16_t fallback) {
    if (value.empty()) {
        return fallback;
    }
    const auto parsed = std::stoul(value);
    if (parsed == 0 || parsed > 65535U) {
        throw CLI::ValidationError{"--port must be in 1..65535"};
    }
    return static_cast<std::uint16_t>(parsed);
}

int run_serve_command(const std::string& serve_port, const std::string& serve_bind) {
    cpipe::server::EditorServerOptions options;
    try {
        options.port = parse_port(serve_port, options.port);
    } catch (const CLI::ValidationError& e) {
        std::cerr << e.what() << '\n';
        return static_cast<int>(CPIPE_BAD_INDEX);
    } catch (const std::exception& e) {
        std::cerr << "invalid --port: " << e.what() << '\n';
        return static_cast<int>(CPIPE_BAD_INDEX);
    }
    if (!serve_bind.empty()) {
        options.bind = serve_bind;
    }

    cpipe_link_all_builtin_nodes();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::server::EditorServer server;
    server.set_registry(&registry);
    std::string error;
    const auto status = server.start(options, &error);
    if (status != CPIPE_OK) {
        std::cerr << error << '\n';
        return static_cast<int>(status);
    }

    std::signal(SIGINT, request_server_stop);
    std::signal(SIGTERM, request_server_stop);
    while (!g_stop_server.load() && server.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    server.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"cpipe"};
    app.allow_extras(false);

    std::filesystem::path input;
    std::filesystem::path pipeline_path;
    std::filesystem::path output;
    std::string serve_port;
    std::string serve_bind;
    std::string serve_pipeline;
    std::string editor_static;
    std::string iqa_pipeline;
    std::string iqa_corpus;
    std::string iqa_report;
    std::string iqa_metrics;
    std::string iqa_baseline;
    std::string bench_pipeline;
    std::string bench_corpus;
    std::string bench_out;

    auto* run = app.add_subcommand("run", "Run a pipeline");
    run->add_option("input", input, "Input DNG or RGBA8 binary")->required();
    run->add_option("-p,--pipeline", pipeline_path, "Pipeline JSON")->required();
    run->add_option("-o,--output", output, "Output HEIF or binary")->required();

    auto* serve = app.add_subcommand("serve", "Start the editor server");
    serve->add_option("--port", serve_port, "Port to listen on");
    serve->add_option("--bind", serve_bind, "Address to bind");
    serve->add_option("--pipeline", serve_pipeline, "Pipeline JSON to serve");
    serve->add_option("--editor-static", editor_static, "Editor static directory");

    auto* info = app.add_subcommand("info", "Inspect cpipe runtime information");
    info->add_subcommand("nodes", "List registered node manifests");
    info->add_subcommand("gpu", "Probe Vulkan GPU capabilities");

    auto* iqa = app.add_subcommand("iqa", "Run image-quality analysis");
    iqa->add_option("pipeline", iqa_pipeline, "Pipeline JSON");
    iqa->add_option("corpus", iqa_corpus, "Corpus directory");
    iqa->add_option("--report", iqa_report, "Report format: json, md, or html");
    iqa->add_option("--metrics", iqa_metrics, "Comma-separated metric list");
    iqa->add_option("--baseline", iqa_baseline, "Previous JSON report");

    auto* bench = app.add_subcommand("bench", "Run end-to-end pipeline timing");
    bench->add_option("--pipeline", bench_pipeline, "Pipeline JSON");
    bench->add_option("--corpus", bench_corpus, "Corpus directory");
    bench->add_option("--out", bench_out, "Output JSON report");

    CLI11_PARSE(app, argc, argv);

    if (*serve) {
        return run_serve_command(serve_port, serve_bind);
    }
    if (*info) {
        return not_implemented("info");
    }
    if (*iqa) {
        return not_implemented("iqa");
    }
    if (*bench) {
        return not_implemented("bench");
    }

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
