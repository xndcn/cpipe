// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/server/EditorProtocol.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

void cpipe_link_all_builtin_nodes();

namespace {

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

int connect_tcp(std::uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    REQUIRE(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return fd;
}

std::string read_until_headers(int fd) {
    std::string response;
    char byte{};
    while (response.find("\r\n\r\n") == std::string::npos) {
        REQUIRE(recv(fd, &byte, 1, 0) == 1);
        response.push_back(byte);
    }
    return response;
}

int connect_ws(std::uint16_t port) {
    const int fd = connect_tcp(port);
    const std::string request =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    REQUIRE(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
    const auto response = read_until_headers(fd);
    REQUIRE(response.starts_with("HTTP/1.1 101"));
    return fd;
}

void send_ws_binary(int fd, const std::vector<std::uint8_t>& payload) {
    REQUIRE(payload.size() < 126U);
    constexpr std::uint8_t mask[] = {0x11, 0x22, 0x33, 0x44};
    std::vector<std::uint8_t> frame;
    frame.reserve(6U + payload.size());
    frame.push_back(0x82);
    frame.push_back(static_cast<std::uint8_t>(0x80U | payload.size()));
    frame.insert(frame.end(), std::begin(mask), std::end(mask));
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame.push_back(static_cast<std::uint8_t>(payload[index] ^ mask[index % 4U]));
    }
    REQUIRE(send(fd, frame.data(), frame.size(), 0) == static_cast<ssize_t>(frame.size()));
}

std::vector<std::uint8_t> recv_ws_binary(int fd) {
    std::uint8_t header[2]{};
    REQUIRE(recv(fd, header, sizeof(header), MSG_WAITALL) == 2);
    REQUIRE((header[0] & 0x0fU) == 0x02U);
    auto length = static_cast<std::size_t>(header[1] & 0x7fU);
    if (length == 126U) {
        std::uint8_t extended[2]{};
        REQUIRE(recv(fd, extended, sizeof(extended), MSG_WAITALL) == 2);
        length = (static_cast<std::size_t>(extended[0]) << 8U) | extended[1];
    }
    std::vector<std::uint8_t> payload(length);
    REQUIRE(recv(fd, payload.data(), payload.size(), MSG_WAITALL) ==
            static_cast<ssize_t>(payload.size()));
    return payload;
}

std::string http_put_pipeline(std::uint16_t port) {
    const auto body =
        nlohmann::json{
            {"$schema", "https://schemas.cpipe.dev/pipeline/v0.4.json"},
            {"version", "0.4"},
            {"id", "ws-smoke"},
            {"inputs", nlohmann::json::array(
                           {{{"port", "raw"}, {"kind", "Image2D"}, {"format", "R8G8B8A8_UNORM"}}})},
            {"nodes", nlohmann::json::array({{{"id", "tone"},
                                              {"type", "com.cpipe.tone.filmic_rgb"},
                                              {"params", nlohmann::json::object()}}})},
            {"edges", nlohmann::json::array()},
        }
            .dump();
    const int fd = connect_tcp(port);
    const std::string request =
        "PUT /api/pipelines/active HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        body;
    REQUIRE(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
    const auto response = read_until_headers(fd);
    close(fd);
    return response;
}

std::string http_post_run(std::uint16_t port) {
    const int fd = connect_tcp(port);
    const std::string request =
        "POST /api/pipelines/active/run HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    REQUIRE(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
    const auto response = read_until_headers(fd);
    close(fd);
    return response;
}

class RunningServer {
public:
    RunningServer() {
        cpipe_link_all_builtin_nodes();
        registry_.load_builtin_nodes();
        port_ = reserve_free_port();
        cpipe::server::EditorServerOptions options;
        options.port = port_;
        server_.set_registry(&registry_);
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
    cpipe::runtime::Registry registry_;
    cpipe::server::EditorServer server_;
};

}  // namespace

TEST_CASE("editor server websocket accepts control frames and emits runtime events") {
    RunningServer server;
    REQUIRE(http_put_pipeline(server.port()).starts_with("HTTP/1.1 200 OK"));

    const int fd = connect_ws(server.port());

    const auto subscribe =
        cpipe::server::encode_frame(cpipe::server::EditorFrameType::Control, 0, 0,
                                    nlohmann::json{{"type", "node.subscribe_thumbnail"},
                                                   {"node_id", "tone"},
                                                   {"port", "rgb"},
                                                   {"max_size", 256},
                                                   {"fps", 5}}
                                        .dump());
    send_ws_binary(fd, subscribe);

    const auto ack = cpipe::server::decode_frame(recv_ws_binary(fd));
    REQUIRE(ack.has_value());
    REQUIRE(ack->type == cpipe::server::EditorFrameType::Ack);
    REQUIRE(nlohmann::json::parse(std::string{ack->payload.begin(), ack->payload.end()}).at("ok") ==
            true);

    const auto response = http_post_run(server.port());
    REQUIRE(response.starts_with("HTTP/1.1 200 OK"));

    const auto profile = cpipe::server::decode_frame(recv_ws_binary(fd));
    const auto log = cpipe::server::decode_frame(recv_ws_binary(fd));
    REQUIRE(profile.has_value());
    REQUIRE(log.has_value());
    REQUIRE(profile->type == cpipe::server::EditorFrameType::Profile);
    REQUIRE(log->type == cpipe::server::EditorFrameType::Log);

    const auto profile_payload =
        nlohmann::json::parse(std::string{profile->payload.begin(), profile->payload.end()});
    REQUIRE(profile_payload.at("type") == "pipeline.profile");
    REQUIRE(profile_payload.at("nodes").at(0).at("node_id") == "tone");
    REQUIRE(profile_payload.at("nodes").at(0).contains("ms"));
    REQUIRE(profile_payload.at("nodes").at(0).contains("mem_kb"));

    const auto log_payload =
        nlohmann::json::parse(std::string{log->payload.begin(), log->payload.end()});
    REQUIRE(log_payload.at("level") == "info");
    REQUIRE(log_payload.at("message").get<std::string>().find("event=pipeline_run") !=
            std::string::npos);

    close(fd);
}
