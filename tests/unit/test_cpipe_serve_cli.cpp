// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct HttpResponse {
    int status{0};
    std::string body;
};

std::uint16_t reserve_free_port() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error{"failed to create probe socket"};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        throw std::runtime_error{"failed to bind probe socket"};
    }

    socklen_t length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        close(fd);
        throw std::runtime_error{"failed to inspect probe socket"};
    }
    const auto port = ntohs(address.sin_port);
    close(fd);
    return port;
}

HttpResponse http_get(std::uint16_t port, const std::string& path) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return {};
    }

    const std::string request =
        "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    REQUIRE(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));

    std::string response;
    char buffer[4096]{};
    for (;;) {
        const auto bytes = recv(fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(bytes));
    }
    close(fd);

    const auto line_end = response.find("\r\n");
    if (line_end == std::string::npos || response.size() < 12U) {
        return {};
    }
    const auto body_start = response.find("\r\n\r\n");
    return HttpResponse{
        .status = std::stoi(response.substr(9U, 3U)),
        .body = body_start == std::string::npos ? std::string{} : response.substr(body_start + 4U)};
}

HttpResponse wait_for_http(std::uint16_t port, const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        auto response = http_get(port, path);
        if (response.status != 0) {
            return response;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    return {};
}

class RunningCommand {
public:
    RunningCommand(std::vector<std::string> args,
                   std::vector<std::pair<std::string, std::string>> env = {}) {
        int stderr_pipe[2]{};
        REQUIRE(pipe(stderr_pipe) == 0);
        stderr_read_ = stderr_pipe[0];
        const auto flags = fcntl(stderr_read_, F_GETFL, 0);
        REQUIRE(fcntl(stderr_read_, F_SETFL, flags | O_NONBLOCK) == 0);

        pid_ = fork();
        REQUIRE(pid_ >= 0);
        if (pid_ == 0) {
            close(stderr_pipe[0]);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[1]);
            for (const auto& [key, value] : env) {
                setenv(key.c_str(), value.c_str(), 1);
            }

            std::vector<std::string> owned;
            owned.push_back(CPIPE_CLI_PATH);
            owned.insert(owned.end(), args.begin(), args.end());
            std::vector<char*> argv;
            for (auto& item : owned) {
                argv.push_back(item.data());
            }
            argv.push_back(nullptr);
            execv(CPIPE_CLI_PATH, argv.data());
            _exit(127);
        }
        close(stderr_pipe[1]);
    }

    RunningCommand(const RunningCommand&) = delete;
    RunningCommand& operator=(const RunningCommand&) = delete;
    ~RunningCommand() {
        stop();
        if (stderr_read_ >= 0) {
            close(stderr_read_);
        }
    }

    void stop() {
        if (pid_ <= 0) {
            return;
        }
        kill(pid_, SIGTERM);
        int status = 0;
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }

    [[nodiscard]] std::string stderr_output() const {
        std::string output;
        char buffer[1024]{};
        for (;;) {
            const auto bytes = read(stderr_read_, buffer, sizeof(buffer));
            if (bytes <= 0) {
                break;
            }
            output.append(buffer, static_cast<std::size_t>(bytes));
        }
        return output;
    }

private:
    pid_t pid_{-1};
    int stderr_read_{-1};
};

std::filesystem::path make_temp_dir(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string{"cpipe_"} + std::string{name} + "_" + std::to_string(getpid()) +
                       "_" + std::to_string(reserve_free_port()));
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path make_editor_static() {
    const auto dir = make_temp_dir("editor_static");
    std::ofstream{dir / "index.html"} << "<!doctype html><title>cpipe</title>editor-ok";
    return dir;
}

std::filesystem::path make_pipeline_file() {
    const auto dir = make_temp_dir("pipeline");
    const auto path = dir / "pipeline.cpipe.json";
    std::ofstream{path} << R"({"version":"0.4","nodes":[],"edges":[]})";
    return path;
}

std::filesystem::path make_home_with_settings(std::uint16_t port,
                                              const std::filesystem::path& editor_static) {
    const auto home = make_temp_dir("home");
    const auto config_dir = home / ".config" / "cpipe";
    std::filesystem::create_directories(config_dir);
    std::ofstream{config_dir / "settings.json"} << "{\"server\":{\"port\":" << port
                                                << ",\"bind\":\"127.0.0.1\",\"editor_static\":\""
                                                << editor_static.string() << "\"}}";
    return home;
}

}  // namespace

TEST_CASE("cpipe serve boots on explicit bind and port") {
    const auto port = reserve_free_port();
    RunningCommand server{{"serve", "--port", std::to_string(port), "--bind", "127.0.0.1"}};
    const auto response = wait_for_http(port, "/api/health");
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"ok\":true") != std::string::npos);
}

TEST_CASE("cpipe serve warns on LAN bind") {
    const auto port = reserve_free_port();
    RunningCommand server{{"serve", "--port", std::to_string(port), "--bind", "0.0.0.0"}};
    REQUIRE(wait_for_http(port, "/api/health").status == 200);
    server.stop();
    const auto stderr_output = server.stderr_output();
    REQUIRE(stderr_output.find("WARNING: binding to 0.0.0.0:" + std::to_string(port)) !=
            std::string::npos);
}

TEST_CASE("cpipe serve mounts editor static directory") {
    const auto port = reserve_free_port();
    const auto editor_static = make_editor_static();
    RunningCommand server{{"serve", "--port", std::to_string(port), "--bind", "127.0.0.1",
                           "--editor-static", editor_static.string()}};
    const auto response = wait_for_http(port, "/editor/");
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("editor-ok") != std::string::npos);
}

TEST_CASE("cpipe serve loads active pipeline file") {
    const auto port = reserve_free_port();
    const auto pipeline = make_pipeline_file();
    RunningCommand server{{"serve", "--port", std::to_string(port), "--bind", "127.0.0.1",
                           "--pipeline", pipeline.string()}};
    const auto response = wait_for_http(port, "/api/pipelines/active");
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"version\":\"0.4\"") != std::string::npos);
}

TEST_CASE("cpipe serve layers settings, env, and CLI overrides") {
    const auto editor_static = make_editor_static();
    const auto settings_port = reserve_free_port();
    const auto env_port = reserve_free_port();
    const auto cli_port = reserve_free_port();
    const auto home = make_home_with_settings(settings_port, editor_static);

    {
        RunningCommand server{{"serve"}, {{"HOME", home.string()}}};
        REQUIRE(wait_for_http(settings_port, "/api/health").status == 200);
        REQUIRE(wait_for_http(settings_port, "/editor/").body.find("editor-ok") !=
                std::string::npos);
    }
    {
        RunningCommand server{
            {"serve"}, {{"HOME", home.string()}, {"CPIPE_SERVER_PORT", std::to_string(env_port)}}};
        REQUIRE(wait_for_http(env_port, "/api/health").status == 200);
    }
    {
        RunningCommand server{
            {"serve", "--port", std::to_string(cli_port)},
            {{"HOME", home.string()}, {"CPIPE_SERVER_PORT", std::to_string(env_port)}}};
        REQUIRE(wait_for_http(cli_port, "/api/health").status == 200);
    }
}
