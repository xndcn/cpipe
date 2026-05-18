// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace cpipe::server {

struct EditorServerOptions {
    std::string bind{"127.0.0.1"};
    std::uint16_t port{4747};
};

class EditorServer {
public:
    EditorServer() = default;
    EditorServer(const EditorServer&) = delete;
    EditorServer& operator=(const EditorServer&) = delete;
    EditorServer(EditorServer&&) = delete;
    EditorServer& operator=(EditorServer&&) = delete;
    ~EditorServer();

    [[nodiscard]] cpipe_status_t start(const EditorServerOptions& options, std::string* error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    std::thread io_thread_;
    std::atomic<void*> loop_{nullptr};
    std::atomic<void*> listen_socket_{nullptr};
    std::atomic<bool> running_{false};

    std::mutex start_mutex_;
    std::condition_variable start_cv_;
    bool start_complete_{false};
    bool start_success_{false};
    std::string start_error_;
};

}  // namespace cpipe::server
