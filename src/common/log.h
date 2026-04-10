// src/common/log.h -- spdlog-backed logger for cpipe
#pragma once
#include <spdlog/spdlog.h>
#include <memory>

namespace cpipe::log {

/// Initialise the "cpipe" logger.  Call once at program start.
/// If CPIPE_LOG_LEVEL env var is set its value overrides `level`.
void init(spdlog::level::level_enum level = spdlog::level::info);

/// Returns the shared "cpipe" logger (creates a default one if not yet
/// initialised).
std::shared_ptr<spdlog::logger> get();

} // namespace cpipe::log

// ── Convenience macros ──────────────────────────────────────────────────────
#define CPIPE_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(::cpipe::log::get(), __VA_ARGS__)
#define CPIPE_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(::cpipe::log::get(), __VA_ARGS__)
#define CPIPE_LOG_INFO(...)     SPDLOG_LOGGER_INFO(::cpipe::log::get(), __VA_ARGS__)
#define CPIPE_LOG_WARN(...)     SPDLOG_LOGGER_WARN(::cpipe::log::get(), __VA_ARGS__)
#define CPIPE_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(::cpipe::log::get(), __VA_ARGS__)
#define CPIPE_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::cpipe::log::get(), __VA_ARGS__)
