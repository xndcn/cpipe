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
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

void cpipe_link_all_builtin_nodes();

namespace {

constexpr int kNotImplementedExit = 100;
std::atomic_bool g_stop_server{false};

struct ServeCommandConfig {
    std::string port;
    std::string bind;
    std::string pipeline;
    std::string editor_static;
};

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

std::optional<std::string> env_string(const char* key) {
    const auto* value = std::getenv(key);
    if (value == nullptr || std::string_view{value}.empty()) {
        return std::nullopt;
    }
    return std::string{value};
}

std::filesystem::path settings_path() {
    if (const auto home = env_string("HOME")) {
        return std::filesystem::path{*home} / ".config" / "cpipe" / "settings.json";
    }
    return {};
}

ServeCommandConfig load_settings_config() {
    const auto path = settings_path();
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        return {};
    }
    std::ifstream input{path};
    const auto document = nlohmann::json::parse(input);
    const auto server = document.value("server", nlohmann::json::object());
    ServeCommandConfig config;
    if (server.contains("port")) {
        config.port = std::to_string(server.at("port").get<std::uint16_t>());
    }
    if (server.contains("bind")) {
        config.bind = server.at("bind").get<std::string>();
    }
    if (server.contains("pipeline")) {
        config.pipeline = server.at("pipeline").get<std::string>();
    }
    if (server.contains("editor_static")) {
        config.editor_static = server.at("editor_static").get<std::string>();
    }
    return config;
}

std::filesystem::path installed_editor_static_path() {
    return std::filesystem::path{CPIPE_INSTALLED_EDITOR_STATIC};
}

ServeCommandConfig default_serve_config() {
    ServeCommandConfig config;
    const auto editor_static = installed_editor_static_path();
    if (std::filesystem::is_regular_file(editor_static / "index.html")) {
        config.editor_static = editor_static.string();
    }
    return config;
}

void apply_settings_config(ServeCommandConfig* config) {
    const auto settings = load_settings_config();
    if (!settings.port.empty()) {
        config->port = settings.port;
    }
    if (!settings.bind.empty()) {
        config->bind = settings.bind;
    }
    if (!settings.pipeline.empty()) {
        config->pipeline = settings.pipeline;
    }
    if (!settings.editor_static.empty()) {
        config->editor_static = settings.editor_static;
    }
}

void apply_env_config(ServeCommandConfig* config) {
    if (const auto value = env_string("CPIPE_SERVER_PORT")) {
        config->port = *value;
    }
    if (const auto value = env_string("CPIPE_SERVER_BIND")) {
        config->bind = *value;
    }
    if (const auto value = env_string("CPIPE_PIPELINE")) {
        config->pipeline = *value;
    }
    if (const auto value = env_string("CPIPE_EDITOR_STATIC")) {
        config->editor_static = *value;
    }
}

ServeCommandConfig resolve_serve_config(const std::string& serve_port,
                                        const std::string& serve_bind,
                                        const std::string& serve_pipeline,
                                        const std::string& editor_static) {
    auto config = default_serve_config();
    apply_settings_config(&config);
    apply_env_config(&config);
    if (!serve_port.empty()) {
        config.port = serve_port;
    }
    if (!serve_bind.empty()) {
        config.bind = serve_bind;
    }
    if (!serve_pipeline.empty()) {
        config.pipeline = serve_pipeline;
    }
    if (!editor_static.empty()) {
        config.editor_static = editor_static;
    }
    return config;
}

bool is_loopback_bind(std::string_view bind) {
    return bind == "localhost" || bind == "::1" || bind.starts_with("127.");
}

void warn_on_lan_bind(const cpipe::server::EditorServerOptions& options) {
    if (is_loopback_bind(options.bind)) {
        return;
    }
    std::cerr << "\033[33mWARNING: binding to " << options.bind << ':' << options.port
              << "; this server has no authentication and may expose your runtime to anyone on "
                 "the LAN. RD-8 documents this risk.\033[0m\n";
}

int run_serve_command(const std::string& serve_port, const std::string& serve_bind,
                      const std::string& serve_pipeline, const std::string& editor_static) {
    const auto config = resolve_serve_config(serve_port, serve_bind, serve_pipeline, editor_static);
    cpipe::server::EditorServerOptions options;
    try {
        options.port = parse_port(config.port, options.port);
    } catch (const CLI::ValidationError& e) {
        std::cerr << e.what() << '\n';
        return static_cast<int>(CPIPE_BAD_INDEX);
    } catch (const std::exception& e) {
        std::cerr << "invalid --port: " << e.what() << '\n';
        return static_cast<int>(CPIPE_BAD_INDEX);
    }
    if (!config.bind.empty()) {
        options.bind = config.bind;
    }
    if (!config.editor_static.empty()) {
        options.editor_static = config.editor_static;
    }
    warn_on_lan_bind(options);

    cpipe_link_all_builtin_nodes();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::server::EditorServer server;
    server.set_registry(&registry);
    if (!config.pipeline.empty()) {
        std::ifstream input{config.pipeline};
        if (!input.good()) {
            std::cerr << "failed to open pipeline: " << config.pipeline << '\n';
            return static_cast<int>(CPIPE_BAD_INDEX);
        }
        std::string pipeline_error;
        const auto document = nlohmann::json::parse(input);
        const auto pipeline_status = server.set_active_pipeline(document, &pipeline_error);
        if (pipeline_status != CPIPE_OK) {
            std::cerr << pipeline_error << '\n';
            return static_cast<int>(pipeline_status);
        }
    }

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

int cpipe_main(int argc, char** argv) {
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
        return run_serve_command(serve_port, serve_bind, serve_pipeline, editor_static);
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

int main(int argc, char** argv) noexcept {
    try {
        return cpipe_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return static_cast<int>(CPIPE_BAD_INDEX);
    }
}
