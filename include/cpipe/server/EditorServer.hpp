// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <atomic>
#include <condition_variable>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>

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

    void set_registry(const runtime::Registry* registry) noexcept;
    [[nodiscard]] cpipe_status_t start(const EditorServerOptions& options, std::string* error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    struct RunRecord {
        std::string status;
        nlohmann::json output_paths;
    };

    [[nodiscard]] nlohmann::json registry_nodes() const;
    [[nodiscard]] nlohmann::json active_pipeline() const;
    [[nodiscard]] cpipe_status_t replace_active_pipeline(const nlohmann::json& pipeline,
                                                         std::string* error);
    [[nodiscard]] cpipe_status_t apply_param_delta(const nlohmann::json& delta, std::string* error);
    [[nodiscard]] nlohmann::json create_run_record();
    [[nodiscard]] nlohmann::json run_record(std::uint64_t run_id) const;

    const runtime::Registry* registry_{nullptr};
    mutable std::mutex session_mutex_;
    nlohmann::json active_pipeline_;
    std::uint64_t next_run_id_{1};
    std::unordered_map<std::uint64_t, RunRecord> runs_;

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
