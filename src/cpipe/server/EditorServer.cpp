// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <libusockets.h>
#include <uwebsockets/App.h>

#include <chrono>
#include <condition_variable>
#include <cpipe/runtime/Trace.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kHealthBody = R"({"ok":true,"abi":{"major":1,"minor":3}})";

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

namespace cpipe::server {

EditorServer::~EditorServer() {
    stop();
}

cpipe_status_t EditorServer::start(const EditorServerOptions& options, std::string* error) {
    if (running_.load()) {
        set_error(error, "editor server is already running");
        return CPIPE_FAILED;
    }

    {
        std::lock_guard lock{start_mutex_};
        start_complete_ = false;
        start_success_ = false;
        start_error_.clear();
    }

    io_thread_ = std::thread{[this, options] {
        try {
            uWS::App app;
            app.get("/api/health", [](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                response->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Cache-Control", "no-store")
                    ->end(kHealthBody);
            });

            loop_.store(uWS::Loop::get());
            app.listen(options.bind, static_cast<int>(options.port), [this](auto* token) {
                {
                    std::lock_guard lock{start_mutex_};
                    listen_socket_.store(token);
                    start_success_ = token != nullptr;
                    if (!start_success_) {
                        start_error_ = "failed to bind editor server";
                    }
                    start_complete_ = true;
                    running_.store(start_success_);
                }
                start_cv_.notify_one();
            });
            app.run();
        } catch (const std::exception& e) {
            {
                std::lock_guard lock{start_mutex_};
                start_success_ = false;
                start_error_ = e.what();
                start_complete_ = true;
            }
            start_cv_.notify_one();
        }

        listen_socket_.store(nullptr);
        loop_.store(nullptr);
        running_.store(false);
    }};

    std::unique_lock lock{start_mutex_};
    if (!start_cv_.wait_for(lock, std::chrono::seconds{5}, [this] { return start_complete_; })) {
        set_error(error, "timed out waiting for editor server to start");
        stop();
        return CPIPE_FAILED;
    }
    if (!start_success_) {
        set_error(error, start_error_);
        lock.unlock();
        stop();
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

void EditorServer::stop() noexcept {
    auto* loop = static_cast<uWS::Loop*>(loop_.load());
    auto* listen_socket = static_cast<us_listen_socket_t*>(listen_socket_.load());
    if (loop != nullptr) {
        loop->defer([listen_socket] {
            if (listen_socket != nullptr) {
                us_listen_socket_close(0, listen_socket);
            }
        });
    }

    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    listen_socket_.store(nullptr);
    loop_.store(nullptr);
    running_.store(false);
}

bool EditorServer::running() const noexcept {
    return running_.load();
}

}  // namespace cpipe::server
