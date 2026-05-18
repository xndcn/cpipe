// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

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

std::string http_get(std::uint16_t port, const std::string& path) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    REQUIRE(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

    const std::string request = "GET " + path +
                                " HTTP/1.1\r\nHost: localhost\r\n"
                                "Connection: close\r\n\r\n";
    REQUIRE(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));

    std::string response;
    char buffer[1024]{};
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

std::string response_body(const std::string& response) {
    const auto split = response.find("\r\n\r\n");
    REQUIRE(split != std::string::npos);
    return response.substr(split + 4U);
}

}  // namespace

TEST_CASE("editor server health route returns ABI JSON") {
    const auto port = reserve_free_port();

    cpipe::server::EditorServer server;
    cpipe::server::EditorServerOptions options;
    options.bind = "127.0.0.1";
    options.port = port;
    std::string error;
    REQUIRE(server.start(options, &error) == CPIPE_OK);

    const auto response = http_get(port, "/api/health");
    server.stop();

    REQUIRE(response.starts_with("HTTP/1.1 200 OK"));
    const auto body = nlohmann::json::parse(response_body(response));
    REQUIRE(body.at("ok") == true);
    REQUIRE(body.at("abi").at("major") == 1);
    REQUIRE(body.at("abi").at("minor") == 3);
}
