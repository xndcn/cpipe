// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/server/EditorProtocol.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

void cpipe_link_all_builtin_nodes();

namespace {

class UniqueFd {
public:
    explicit UniqueFd(int fd) noexcept : fd_{fd} {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_{other.fd_} {
        other.fd_ = -1;
    }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            close_if_open();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~UniqueFd() {
        close_if_open();
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

private:
    void close_if_open() noexcept {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    int fd_{-1};
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

std::optional<std::vector<std::uint8_t>> recv_ws_binary(int fd, std::chrono::milliseconds timeout) {
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    const auto ready = poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready == 0) {
        return std::nullopt;
    }
    REQUIRE(ready == 1);

    std::uint8_t header[2]{};
    REQUIRE(recv(fd, header, sizeof(header), MSG_WAITALL) == 2);
    REQUIRE((header[0] & 0x0fU) == 0x02U);
    auto length = static_cast<std::size_t>(header[1] & 0x7fU);
    if (length == 126U) {
        std::uint8_t extended[2]{};
        REQUIRE(recv(fd, extended, sizeof(extended), MSG_WAITALL) == 2);
        length = (static_cast<std::size_t>(extended[0]) << 8U) | extended[1];
    } else if (length == 127U) {
        std::uint8_t extended[8]{};
        REQUIRE(recv(fd, extended, sizeof(extended), MSG_WAITALL) == 8);
        length = 0;
        for (const auto byte : extended) {
            length = (length << 8U) | byte;
        }
    }

    std::vector<std::uint8_t> payload(length);
    REQUIRE(recv(fd, payload.data(), payload.size(), MSG_WAITALL) ==
            static_cast<ssize_t>(payload.size()));
    return payload;
}

cpipe::server::EditorFrame recv_editor_frame(int fd) {
    const auto payload = recv_ws_binary(fd, std::chrono::seconds{2});
    REQUIRE(payload.has_value());
    const auto frame = cpipe::server::decode_frame(*payload);
    REQUIRE(frame.has_value());
    return *frame;
}

void send_control(int fd, const nlohmann::json& payload) {
    send_ws_binary(fd, cpipe::server::encode_frame(cpipe::server::EditorFrameType::Control, 0, 0,
                                                   payload.dump()));
    const auto ack = recv_editor_frame(fd);
    REQUIRE(ack.type == cpipe::server::EditorFrameType::Ack);
    REQUIRE(nlohmann::json::parse(std::string{ack.payload.begin(), ack.payload.end()}).at("ok") ==
            true);
}

std::string http_put_pipeline(std::uint16_t port) {
    const auto body =
        nlohmann::json{
            {"$schema", "https://schemas.cpipe.dev/pipeline/v0.4.json"},
            {"version", "0.4"},
            {"id", "thumbnail-smoke"},
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

bool is_webp(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 12U && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
           bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
           bytes[11] == 'P';
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
        REQUIRE(http_put_pipeline(port_).starts_with("HTTP/1.1 200 OK"));
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

TEST_CASE("thumbnail subscription emits WebP frames and supports unsubscribe") {
    RunningServer server;
    UniqueFd fd{connect_ws(server.port())};

    send_control(fd.get(), {{"type", "node.subscribe_thumbnail"},
                            {"node_id", "tone"},
                            {"port", "rgb"},
                            {"max_size", 64},
                            {"fps", 5}});

    REQUIRE(http_post_run(server.port()).starts_with("HTTP/1.1 200 OK"));
    const auto thumbnail = recv_editor_frame(fd.get());
    REQUIRE(thumbnail.type == cpipe::server::EditorFrameType::Thumbnail);
    REQUIRE(is_webp(thumbnail.payload));
    (void)recv_editor_frame(fd.get());
    (void)recv_editor_frame(fd.get());

    send_control(fd.get(),
                 {{"type", "node.unsubscribe_thumbnail"}, {"node_id", "tone"}, {"port", "rgb"}});
    REQUIRE(http_post_run(server.port()).starts_with("HTTP/1.1 200 OK"));
    const auto first_after_unsubscribe = recv_editor_frame(fd.get());
    REQUIRE(first_after_unsubscribe.type != cpipe::server::EditorFrameType::Thumbnail);
}

TEST_CASE("thumbnail subscription shares one frame across subscribers and applies cap") {
    RunningServer server;
    std::vector<UniqueFd> clients;
    for (int index = 0; index < 5; ++index) {
        clients.emplace_back(connect_ws(server.port()));
        send_control(clients.back().get(), {{"type", "node.subscribe_thumbnail"},
                                            {"node_id", "tone"},
                                            {"port", "rgb"},
                                            {"max_size", 64},
                                            {"fps", 60}});
    }

    REQUIRE(http_post_run(server.port()).starts_with("HTTP/1.1 200 OK"));
    std::vector<std::uint8_t> first_thumbnail;
    for (const auto& fd : clients) {
        const auto frame = recv_editor_frame(fd.get());
        REQUIRE(frame.type == cpipe::server::EditorFrameType::Thumbnail);
        REQUIRE(is_webp(frame.payload));
        if (first_thumbnail.empty()) {
            first_thumbnail = frame.payload;
        } else {
            REQUIRE(frame.payload == first_thumbnail);
        }
        (void)recv_editor_frame(fd.get());
        (void)recv_editor_frame(fd.get());
    }

    REQUIRE(http_post_run(server.port()).starts_with("HTTP/1.1 200 OK"));
    for (const auto& fd : clients) {
        const auto first = recv_editor_frame(fd.get());
        const auto second = recv_editor_frame(fd.get());
        REQUIRE(first.type != cpipe::server::EditorFrameType::Thumbnail);
        REQUIRE(second.type != cpipe::server::EditorFrameType::Thumbnail);
    }
}
