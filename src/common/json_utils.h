// src/common/json_utils.h -- thin nlohmann/json wrappers returning cpipe::expected
#pragma once
#include "error.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string_view>

namespace cpipe::json {

/// Parse a JSON string; returns Error on parse failure.
expected<nlohmann::json, Error> parse_string(std::string_view str);

/// Read and parse a JSON file; returns Error if the file cannot be opened or
/// contains invalid JSON.
expected<nlohmann::json, Error> parse_file(const std::filesystem::path& path);

/// Retrieve a typed value from a JSON object by key.
/// Returns Error if the key is absent or the type doesn't match.
template <typename T>
expected<T, Error> get(const nlohmann::json& j, const std::string& key) {
    auto it = j.find(key);
    if (it == j.end()) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            "JSON key not found: " + key));
    }
    try {
        return it->template get<T>();
    } catch (const nlohmann::json::exception& e) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            std::string("JSON type error for key '") + key + "': " + e.what()));
    }
}

} // namespace cpipe::json
