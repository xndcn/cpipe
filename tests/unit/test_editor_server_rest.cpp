// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

void cpipe_link_all_builtin_nodes();

namespace {

struct HttpResponse {
    int status{0};
    nlohmann::json body;
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

std::string raw_request(std::uint16_t port, const std::string& method, const std::string& path,
                        const std::string& body = {}) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    REQUIRE(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

    std::string request = method + " " + path +
                          " HTTP/1.1\r\nHost: localhost\r\n"
                          "Content-Type: application/json\r\nConnection: close\r\n";
    if (!body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n";
    request += body;
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
    return response;
}

HttpResponse request_json(std::uint16_t port, const std::string& method, const std::string& path,
                          const nlohmann::json& body = nullptr) {
    const auto response =
        raw_request(port, method, path, body.is_null() ? std::string{} : body.dump());
    const auto line_end = response.find("\r\n");
    REQUIRE(line_end != std::string::npos);
    const auto status = std::stoi(response.substr(9U, 3U));
    const auto body_start = response.find("\r\n\r\n");
    REQUIRE(body_start != std::string::npos);
    return HttpResponse{.status = status,
                        .body = nlohmann::json::parse(response.substr(body_start + 4U))};
}

nlohmann::json sample_pipeline() {
    return {
        {"$schema", "https://schemas.cpipe.dev/pipeline/v0.4.json"},
        {"version", "0.4"},
        {"id", "rest-smoke"},
        {"inputs", nlohmann::json::array(
                       {{{"port", "raw"}, {"kind", "Image2D"}, {"format", "R8G8B8A8_UNORM"}}})},
        {"nodes", nlohmann::json::array({{{"id", "tone"},
                                          {"type", "com.cpipe.tone.filmic_rgb"},
                                          {"params", nlohmann::json::object()}}})},
        {"edges", nlohmann::json::array()},
    };
}

class RunningServer {
public:
    explicit RunningServer(const cpipe::runtime::Registry* registry = nullptr) {
        port_ = reserve_free_port();
        cpipe::server::EditorServerOptions options;
        options.bind = "127.0.0.1";
        options.port = port_;
        server_.set_registry(registry);
        std::string error;
        REQUIRE(server_.start(options, &error) == CPIPE_OK);
    }

    ~RunningServer() {
        server_.stop();
    }

    [[nodiscard]] std::uint16_t port() const noexcept {
        return port_;
    }

private:
    std::uint16_t port_{0};
    cpipe::server::EditorServer server_;
};

}  // namespace

TEST_CASE("editor server serves embedded schemas and registered manifests") {
    cpipe_link_all_builtin_nodes();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    RunningServer server{&registry};

    const auto node_schema = request_json(server.port(), "GET", "/api/schemas/node");
    REQUIRE(node_schema.status == 200);
    REQUIRE(node_schema.body.at("$id") == "https://schemas.cpipe.dev/node/v0.2.json");

    const auto pipeline_schema = request_json(server.port(), "GET", "/api/schemas/pipeline");
    REQUIRE(pipeline_schema.status == 200);
    REQUIRE(pipeline_schema.body.at("$id") == "https://schemas.cpipe.dev/pipeline/v0.4.json");

    const auto registry_response = request_json(server.port(), "GET", "/api/registry/nodes");
    REQUIRE(registry_response.status == 200);
    REQUIRE(registry_response.body.at("ok") == true);
    REQUIRE(registry_response.body.at("data").at("nodes").size() >= 18);
}

TEST_CASE("editor server active pipeline endpoints round-trip params and runs") {
    cpipe_link_all_builtin_nodes();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    RunningServer server{&registry};

    const auto pipeline = sample_pipeline();
    const auto put = request_json(server.port(), "PUT", "/api/pipelines/active", pipeline);
    REQUIRE(put.status == 200);
    REQUIRE(put.body.at("ok") == true);

    const auto get = request_json(server.port(), "GET", "/api/pipelines/active");
    REQUIRE(get.status == 200);
    REQUIRE(get.body.at("data").at("pipeline") == pipeline);

    const auto params = request_json(server.port(), "POST", "/api/pipelines/active/params",
                                     {{"node_id", "tone"}, {"key", "ev"}, {"value", 0.5}});
    REQUIRE(params.status == 200);
    REQUIRE(params.body.at("ok") == true);

    const auto updated = request_json(server.port(), "GET", "/api/pipelines/active");
    REQUIRE(updated.body.at("data").at("pipeline").at("nodes").at(0).at("params").at("ev") == 0.5);

    const auto run = request_json(server.port(), "POST", "/api/pipelines/active/run");
    REQUIRE(run.status == 200);
    REQUIRE(run.body.at("ok") == true);
    const auto run_id = run.body.at("data").at("run_id").get<std::uint64_t>();

    const auto status =
        request_json(server.port(), "GET", "/api/pipelines/active/runs/" + std::to_string(run_id));
    REQUIRE(status.status == 200);
    REQUIRE(status.body.at("data").at("status") == "completed");
}
