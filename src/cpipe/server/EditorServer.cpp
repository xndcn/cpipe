// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <libusockets.h>
#include <uwebsockets/App.h>

#include <chrono>
#include <condition_variable>
#include <cpipe/runtime/Trace.hpp>
#include <cpipe/server/EditorProtocol.hpp>
#include <cpipe/server/EditorServer.hpp>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

void send_json_frame(ServerWebSocket* ws, EditorFrameType type, const nlohmann::json& payload) {
    const auto encoded = encode_frame(type, 0, 0, payload.dump());
    ws->send(std::string_view{reinterpret_cast<const char*>(encoded.data()), encoded.size()},
             uWS::OpCode::BINARY);
}

}  // namespace

namespace cpipe::server {

EditorServer::~EditorServer() {
    stop();
}

void EditorServer::set_registry(const runtime::Registry* registry) noexcept {
    registry_ = registry;
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
                             if (payload.value("type", "") == "node.update_param") {
                                 std::string route_error;
                                 const auto status = apply_param_delta(payload, &route_error);
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
                 .close = [this](auto* ws, int /*code*/,
                                 std::string_view /*message*/) { ws_clients_.erase(ws); }});

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

void EditorServer::broadcast_json_frame(EditorFrameType frame_type, const nlohmann::json& payload) {
    const auto encoded = encode_frame(frame_type, 0, 0, payload.dump());
    const std::string_view bytes{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    for (auto* client : ws_clients_) {
        static_cast<ServerWebSocket*>(client)->send(bytes, uWS::OpCode::BINARY);
    }
}

}  // namespace cpipe::server
