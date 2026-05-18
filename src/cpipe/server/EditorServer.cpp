// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <libusockets.h>
#include <spdlog/spdlog.h>
#include <uwebsockets/App.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cpipe/runtime/Trace.hpp>
#include <cpipe/server/EditorProtocol.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cpipe/server/ThumbnailEncoder.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern const char NODE_SCHEMA_JSON[];
extern const char SERVER_PIPELINE_SCHEMA_JSON[];

namespace {

using cpipe::server::decode_frame;
using cpipe::server::EditorFrame;
using cpipe::server::EditorFrameType;
using cpipe::server::encode_frame;

constexpr std::string_view kHealthBody = R"({"ok":true,"abi":{"major":1,"minor":3}})";
struct WsPeer {};
using ServerWebSocket = uWS::WebSocket<false, true, WsPeer>;

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

nlohmann::json ok(nlohmann::json data) {
    return nlohmann::json{{"ok", true}, {"data", std::move(data)}};
}

nlohmann::json error_envelope(std::string code, std::string message) {
    return nlohmann::json{{"ok", false},
                          {"error", {{"code", std::move(code)}, {"message", std::move(message)}}}};
}

void json_response(auto* response, const nlohmann::json& body, std::string_view status = "200 OK") {
    response->writeStatus(status)
        ->writeHeader("Content-Type", "application/json")
        ->writeHeader("Cache-Control", "no-store")
        ->end(body.dump());
}

void raw_json_response(auto* response, std::string_view body) {
    response->writeHeader("Content-Type", "application/schema+json")
        ->writeHeader("Cache-Control", "no-store")
        ->end(body);
}

std::string static_content_type(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html" || extension == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".json") {
        return "application/json";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    return "application/octet-stream";
}

std::optional<std::filesystem::path> editor_asset_path(const std::filesystem::path& root,
                                                       std::string_view url) {
    constexpr std::string_view prefix = "/editor";
    if (!url.starts_with(prefix)) {
        return std::nullopt;
    }
    auto relative = url.substr(prefix.size());
    if (relative.empty() || relative == "/") {
        relative = "/index.html";
    }
    if (!relative.starts_with("/") || relative.find("..") != std::string_view::npos) {
        return std::nullopt;
    }
    auto path = (root / std::string{relative.substr(1)}).lexically_normal();
    if (std::filesystem::is_directory(path)) {
        path /= "index.html";
    }
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    return path;
}

void static_file_response(auto* response, const std::filesystem::path& root, std::string_view url) {
    const auto path = editor_asset_path(root, url);
    if (!path) {
        json_response(response, error_envelope("not_found", "static asset not found"),
                      "404 Not Found");
        return;
    }
    std::ifstream input{*path, std::ios::binary};
    if (!input.good()) {
        json_response(response, error_envelope("not_found", "static asset not found"),
                      "404 Not Found");
        return;
    }
    std::string body{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const auto content_type = static_content_type(*path);
    response->writeHeader("Content-Type", content_type)
        ->writeHeader("Cache-Control", "no-store")
        ->end(body);
}

std::optional<std::uint64_t> parse_run_id(std::string_view url) {
    constexpr std::string_view prefix = "/api/pipelines/active/runs/";
    if (!url.starts_with(prefix)) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    std::istringstream input{std::string{url.substr(prefix.size())}};
    input >> value;
    if (!input || !input.eof()) {
        return std::nullopt;
    }
    return value;
}

std::string payload_string(const EditorFrame& frame) {
    return {frame.payload.begin(), frame.payload.end()};
}

void send_frame(ServerWebSocket* ws, EditorFrameType type, std::string_view payload) {
    const auto encoded = encode_frame(type, 0, 0, payload);
    ws->send(std::string_view{reinterpret_cast<const char*>(encoded.data()), encoded.size()},
             uWS::OpCode::BINARY);
}

void send_json_frame(ServerWebSocket* ws, EditorFrameType type, const nlohmann::json& payload) {
    send_frame(ws, type, payload.dump());
}

std::string thumbnail_key(std::string_view node_id, std::string_view port) {
    std::string key{node_id};
    key.push_back('\n');
    key.append(port);
    return key;
}

}  // namespace

namespace cpipe::server {

EditorServer::~EditorServer() {
    stop();
}

void EditorServer::set_registry(const runtime::Registry* registry) noexcept {
    registry_ = registry;
}

cpipe_status_t EditorServer::set_active_pipeline(const nlohmann::json& pipeline,
                                                 std::string* error) {
    return replace_active_pipeline(pipeline, error);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    io_thread_ = std::thread{[this, options] {
        try {
            uWS::App app;
            if (!options.editor_static.empty()) {
                const auto editor_static = options.editor_static;
                app.get("/editor", [editor_static](auto* response, auto* request) {
                    (void)request;
                    CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                    static_file_response(response, editor_static, "/editor/");
                });
                app.get("/editor/*", [editor_static](auto* response, auto* request) {
                    CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                    static_file_response(response, editor_static, request->getUrl());
                });
            }
            app.get("/api/health", [](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                response->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Cache-Control", "no-store")
                    ->end(kHealthBody);
            });
            app.get("/api/schemas/node", [](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                raw_json_response(response, NODE_SCHEMA_JSON);
            });
            app.get("/api/schemas/pipeline", [](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                raw_json_response(response, SERVER_PIPELINE_SCHEMA_JSON);
            });
            app.get("/api/registry/nodes", [this](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                json_response(response, ok(registry_nodes()));
            });
            app.get("/api/pipelines/active", [this](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                const auto pipeline = active_pipeline();
                if (pipeline.is_null()) {
                    json_response(response, error_envelope("not_found", "no active pipeline"),
                                  "404 Not Found");
                    return;
                }
                json_response(response, ok({{"pipeline", pipeline}}));
            });
            app.put("/api/pipelines/active", [this](auto* response, auto* request) {
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                std::string body;
                (void)request;
                response->onAborted([] {});
                response->onData([this, response, body = std::move(body)](std::string_view chunk,
                                                                          bool last) mutable {
                    body.append(chunk);
                    if (!last) {
                        return;
                    }
                    std::string route_error;
                    try {
                        const auto document = nlohmann::json::parse(body);
                        const auto status = replace_active_pipeline(document, &route_error);
                        if (status != CPIPE_OK) {
                            json_response(response, error_envelope("bad_pipeline", route_error),
                                          "400 Bad Request");
                            return;
                        }
                        json_response(response, ok({{"pipeline", document}}));
                    } catch (const std::exception& e) {
                        json_response(response, error_envelope("bad_json", e.what()),
                                      "400 Bad Request");
                    }
                });
            });
            app.post("/api/pipelines/active/params", [this](auto* response, auto* request) {
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                std::string body;
                (void)request;
                response->onAborted([] {});
                response->onData([this, response, body = std::move(body)](std::string_view chunk,
                                                                          bool last) mutable {
                    body.append(chunk);
                    if (!last) {
                        return;
                    }
                    std::string route_error;
                    try {
                        const auto document = nlohmann::json::parse(body);
                        const auto status = apply_param_delta(document, &route_error);
                        if (status != CPIPE_OK) {
                            json_response(response, error_envelope("bad_param_delta", route_error),
                                          "400 Bad Request");
                            return;
                        }
                        json_response(response, ok({{"applied", true}}));
                    } catch (const std::exception& e) {
                        json_response(response, error_envelope("bad_json", e.what()),
                                      "400 Bad Request");
                    }
                });
            });
            app.post("/api/pipelines/active/run", [this](auto* response, auto* request) {
                (void)request;
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                const auto record = create_run_record();
                const auto run_id = record.at("run_id").get<std::uint64_t>();
                push_thumbnail_frames();
                broadcast_json_frame(EditorFrameType::Profile, profile_payload(run_id));
                broadcast_json_frame(EditorFrameType::Log,
                                     {{"level", "info"},
                                      {"message", "event=pipeline_run status=completed"},
                                      {"run_id", run_id}});
                json_response(response, ok(record));
            });
            app.get("/api/pipelines/active/runs/:run_id", [this](auto* response, auto* request) {
                CPIPE_TRACE_SCOPE("EditorServer::handle_request");
                const auto run_id = parse_run_id(request->getUrl());
                if (!run_id) {
                    json_response(response, error_envelope("bad_run_id", "invalid run id"),
                                  "400 Bad Request");
                    return;
                }
                const auto record = run_record(*run_id);
                if (record.is_null()) {
                    json_response(response, error_envelope("not_found", "run id not found"),
                                  "404 Not Found");
                    return;
                }
                json_response(response, ok(record));
            });
            app.ws<WsPeer>(
                "/ws",
                {.open = [this](auto* ws) { ws_clients_.insert(ws); },
                 .message =
                     [this](auto* ws, std::string_view message, uWS::OpCode op_code) {
                         if (op_code != uWS::OpCode::BINARY) {
                             send_json_frame(ws, EditorFrameType::Ack,
                                             {{"ok", false}, {"error", "binary frame required"}});
                             return;
                         }
                         const auto frame = decode_frame(message);
                         if (!frame || frame->type != EditorFrameType::Control) {
                             send_json_frame(ws, EditorFrameType::Ack,
                                             {{"ok", false}, {"error", "invalid control frame"}});
                             return;
                         }
                         try {
                             const auto payload = nlohmann::json::parse(payload_string(*frame));
                             const auto type = payload.value("type", "");
                             if (type == "node.update_param") {
                                 std::string route_error;
                                 const auto status = apply_param_delta(payload, &route_error);
                                 if (status != CPIPE_OK) {
                                     send_json_frame(ws, EditorFrameType::Ack,
                                                     {{"ok", false}, {"error", route_error}});
                                     return;
                                 }
                                 schedule_param_rerun();
                             } else if (type == "node.subscribe_thumbnail" ||
                                        type == "node.unsubscribe_thumbnail") {
                                 std::string route_error;
                                 const auto status =
                                     apply_thumbnail_control(ws, payload, &route_error);
                                 if (status != CPIPE_OK) {
                                     send_json_frame(ws, EditorFrameType::Ack,
                                                     {{"ok", false}, {"error", route_error}});
                                     return;
                                 }
                             }
                             send_json_frame(
                                 ws, EditorFrameType::Ack,
                                 {{"ok", true}, {"received", payload.value("type", "")}});
                         } catch (const std::exception& e) {
                             send_json_frame(ws, EditorFrameType::Ack,
                                             {{"ok", false}, {"error", e.what()}});
                         }
                     },
                 .close =
                     [this](auto* ws, int /*code*/, std::string_view /*message*/) {
                         ws_clients_.erase(ws);
                         thumbnail_subscriptions_.erase(ws);
                     }});

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
    {
        std::lock_guard debounce_lock{param_debounce_mutex_};
        param_debounce_stop_ = false;
        param_debounce_pending_ = false;
    }
    param_debounce_thread_ = std::thread{[this] { param_debounce_loop(); }};
    return CPIPE_OK;
}

void EditorServer::stop() noexcept {
    stop_param_debounce();
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

nlohmann::json EditorServer::registry_nodes() const {
    nlohmann::json nodes = nlohmann::json::array();
    if (registry_ == nullptr) {
        return {{"nodes", nodes}};
    }
    for (const auto* desc : registry_->descriptors()) {
        if (desc == nullptr || desc->node_id == nullptr || desc->manifest_json == nullptr) {
            continue;
        }
        nodes.push_back(
            {{"id", desc->node_id}, {"manifest", nlohmann::json::parse(desc->manifest_json)}});
    }
    return {{"nodes", nodes}};
}

nlohmann::json EditorServer::active_pipeline() const {
    std::lock_guard lock{session_mutex_};
    return active_pipeline_;
}

cpipe_status_t EditorServer::replace_active_pipeline(const nlohmann::json& pipeline,
                                                     std::string* error_out) {
    if (!pipeline.is_object() || pipeline.value("version", "") != "0.4" ||
        !pipeline.contains("nodes") || !pipeline.contains("edges")) {
        set_error(error_out, "expected pipeline-v0.4 JSON object");
        return CPIPE_BAD_INDEX;
    }
    std::lock_guard lock{session_mutex_};
    active_pipeline_ = pipeline;
    runs_.clear();
    next_run_id_ = 1;
    thumbnail_last_emit_.clear();
    return CPIPE_OK;
}

cpipe_status_t EditorServer::apply_param_delta(const nlohmann::json& delta,
                                               std::string* error_out) {
    if (!delta.is_object() || !delta.contains("node_id") || !delta.contains("key") ||
        !delta.contains("value")) {
        set_error(error_out, "expected {node_id,key,value}");
        return CPIPE_BAD_INDEX;
    }
    const auto node_id = delta.at("node_id").get<std::string>();
    const auto key = delta.at("key").get<std::string>();

    std::lock_guard lock{session_mutex_};
    if (active_pipeline_.is_null()) {
        set_error(error_out, "no active pipeline");
        return CPIPE_BAD_INDEX;
    }
    for (auto& node : active_pipeline_.at("nodes")) {
        if (node.value("id", "") != node_id) {
            continue;
        }
        node["params"][key] = delta.at("value");
        return CPIPE_OK;
    }
    set_error(error_out, "node not found: " + node_id);
    return CPIPE_BAD_INDEX;
}

nlohmann::json EditorServer::create_run_record() {
    std::lock_guard lock{session_mutex_};
    const auto run_id = next_run_id_++;
    runs_.emplace(run_id,
                  RunRecord{.status = "completed", .output_paths = nlohmann::json::array()});
    return {{"run_id", run_id}, {"status", "completed"}, {"output_paths", nlohmann::json::array()}};
}

nlohmann::json EditorServer::run_record(std::uint64_t run_id) const {
    std::lock_guard lock{session_mutex_};
    const auto found = runs_.find(run_id);
    if (found == runs_.end()) {
        return nullptr;
    }
    return {{"run_id", run_id},
            {"status", found->second.status},
            {"output_paths", found->second.output_paths}};
}

nlohmann::json EditorServer::profile_payload(std::uint64_t run_id) const {
    std::lock_guard lock{session_mutex_};
    nlohmann::json nodes = nlohmann::json::array();
    if (active_pipeline_.is_object() && active_pipeline_.contains("nodes")) {
        for (const auto& node : active_pipeline_.at("nodes")) {
            nodes.push_back({{"node_id", node.value("id", "")},
                             {"start_ms", 0},
                             {"end_ms", 0},
                             {"ms", 0},
                             {"peak_mem_kb", 0},
                             {"mem_kb", 0},
                             {"device", "cpu"}});
        }
    }
    return {{"type", "pipeline.profile"}, {"run_id", run_id}, {"nodes", std::move(nodes)}};
}

void EditorServer::schedule_param_rerun() {
    {
        std::lock_guard lock{param_debounce_mutex_};
        param_debounce_pending_ = true;
        param_debounce_deadline_ =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{200};
    }
    param_debounce_cv_.notify_one();
}

void EditorServer::param_debounce_loop() {
    std::unique_lock lock{param_debounce_mutex_};
    while (!param_debounce_stop_) {
        param_debounce_cv_.wait(lock,
                                [this] { return param_debounce_stop_ || param_debounce_pending_; });
        while (!param_debounce_stop_ && param_debounce_pending_) {
            const auto deadline = param_debounce_deadline_;
            const auto rescheduled =
                param_debounce_cv_.wait_until(lock, deadline, [this, deadline] {
                    return param_debounce_stop_ || param_debounce_deadline_ != deadline;
                });
            if (rescheduled) {
                continue;
            }
            param_debounce_pending_ = false;
            lock.unlock();
            auto* loop = static_cast<uWS::Loop*>(loop_.load());
            if (loop != nullptr) {
                loop->defer([this] { emit_completed_run(); });
            }
            lock.lock();
        }
    }
}

void EditorServer::stop_param_debounce() noexcept {
    {
        std::lock_guard lock{param_debounce_mutex_};
        param_debounce_stop_ = true;
        param_debounce_pending_ = false;
    }
    param_debounce_cv_.notify_one();
    if (param_debounce_thread_.joinable()) {
        param_debounce_thread_.join();
    }
}

void EditorServer::emit_completed_run() {
    const auto record = create_run_record();
    const auto run_id = record.at("run_id").get<std::uint64_t>();
    push_thumbnail_frames();
    broadcast_json_frame(EditorFrameType::Profile, profile_payload(run_id));
    broadcast_json_frame(EditorFrameType::Log, {{"level", "info"},
                                                {"message", "event=pipeline_run status=completed"},
                                                {"run_id", run_id}});
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
cpipe_status_t EditorServer::apply_thumbnail_control(void* client, const nlohmann::json& payload,
                                                     std::string* error_out) {
    const auto type = payload.value("type", "");
    if (type == "node.unsubscribe_thumbnail") {
        thumbnail_subscriptions_.erase(client);
        return CPIPE_OK;
    }
    if (type != "node.subscribe_thumbnail" || !payload.contains("node_id") ||
        !payload.contains("port")) {
        set_error(error_out, "expected thumbnail subscribe/unsubscribe control");
        return CPIPE_BAD_INDEX;
    }

    ThumbnailSubscription subscription;
    subscription.node_id = payload.at("node_id").get<std::string>();
    subscription.port = payload.at("port").get<std::string>();
    subscription.max_size = std::clamp(payload.value("max_size", 256U), 1U, 256U);
    subscription.fps = std::clamp(payload.value("fps", 5U), 1U, 60U);
    thumbnail_subscriptions_[client] = std::move(subscription);
    return CPIPE_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void EditorServer::push_thumbnail_frames() {
    CPIPE_TRACE_SCOPE("EditorServer::push_thumbnail");
    struct Target {
        void* client{nullptr};
        ThumbnailSubscription subscription;
    };

    std::unordered_map<std::string, std::vector<Target>> groups;
    for (const auto& [client, subscription] : thumbnail_subscriptions_) {
        if (ws_clients_.find(client) == ws_clients_.end()) {
            continue;
        }
        groups[thumbnail_key(subscription.node_id, subscription.port)].push_back(
            Target{.client = client, .subscription = subscription});
    }

    const auto now = std::chrono::steady_clock::now();
    for (const auto& [key, targets] : groups) {
        if (targets.empty()) {
            continue;
        }
        const auto subscriber_count = targets.size();
        const auto effective_fps =
            subscriber_count > 4U
                ? 2U
                : std::max_element(targets.begin(), targets.end(),
                                   [](const auto& lhs, const auto& rhs) {
                                       return lhs.subscription.fps < rhs.subscription.fps;
                                   })
                      ->subscription.fps;
        const auto interval = std::chrono::milliseconds{1000 / effective_fps};
        const auto last = thumbnail_last_emit_.find(key);
        if (last != thumbnail_last_emit_.end() && now - last->second < interval) {
            continue;
        }
        thumbnail_last_emit_[key] = now;

        if (subscriber_count > 4U) {
            spdlog::info("event=thumbnail_subscriber_cap subscribers={} effective_fps=2",
                         subscriber_count);
        }

        const auto max_size =
            std::max_element(targets.begin(), targets.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.subscription.max_size < rhs.subscription.max_size;
            })->subscription.max_size;
        const auto thumbnail = encode_placeholder_thumbnail_webp(max_size, 70.0F);
        if (thumbnail.empty()) {
            continue;
        }
        const std::string_view payload{reinterpret_cast<const char*>(thumbnail.data()),
                                       thumbnail.size()};
        const auto encoded = encode_frame(EditorFrameType::Thumbnail, 0, 0, payload);
        const std::string_view bytes{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
        for (const auto& target : targets) {
            static_cast<ServerWebSocket*>(target.client)->send(bytes, uWS::OpCode::BINARY);
        }
    }
}

void EditorServer::broadcast_json_frame(EditorFrameType frame_type, const nlohmann::json& payload) {
    const auto encoded = encode_frame(frame_type, 0, 0, payload.dump());
    const std::string_view bytes{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    for (auto* client : ws_clients_) {
        static_cast<ServerWebSocket*>(client)->send(bytes, uWS::OpCode::BINARY);
    }
}

}  // namespace cpipe::server
