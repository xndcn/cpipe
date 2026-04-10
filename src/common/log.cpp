// src/common/log.cpp
#include "log.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstdlib>
#include <mutex>
#include <string_view>

namespace cpipe::log {

namespace {

spdlog::level::level_enum level_from_env(spdlog::level::level_enum fallback) {
    const char* env = std::getenv("CPIPE_LOG_LEVEL");
    if (!env) return fallback;
    std::string_view sv{env};
    if (sv == "trace")    return spdlog::level::trace;
    if (sv == "debug")    return spdlog::level::debug;
    if (sv == "info")     return spdlog::level::info;
    if (sv == "warn")     return spdlog::level::warn;
    if (sv == "error")    return spdlog::level::err;
    if (sv == "critical") return spdlog::level::critical;
    if (sv == "off")      return spdlog::level::off;
    return fallback;
}

std::once_flag                     s_init_flag;
std::shared_ptr<spdlog::logger>    s_logger;

void ensure_logger(spdlog::level::level_enum level) {
    std::call_once(s_init_flag, [level] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        s_logger = std::make_shared<spdlog::logger>("cpipe", std::move(sink));
        s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
        s_logger->set_level(level_from_env(level));
        spdlog::register_logger(s_logger);
    });
}

} // namespace

void init(spdlog::level::level_enum level) {
    ensure_logger(level);
    // call_once already ran: just update the level.
    s_logger->set_level(level_from_env(level));
}

std::shared_ptr<spdlog::logger> get() {
    ensure_logger(spdlog::level::info);
    return s_logger;
}

} // namespace cpipe::log
