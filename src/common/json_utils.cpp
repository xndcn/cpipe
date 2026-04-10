// src/common/json_utils.cpp
#include "json_utils.h"
#include <fstream>

namespace cpipe::json {

expected<nlohmann::json, Error> parse_string(std::string_view str) {
    try {
        return nlohmann::json::parse(str);
    } catch (const nlohmann::json::exception& e) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            std::string("JSON parse error: ") + e.what()));
    }
}

expected<nlohmann::json, Error> parse_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_IO,
            "Cannot open file: " + path.string()));
    }
    try {
        return nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception& e) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            "JSON parse error in " + path.string() + ": " + e.what()));
    }
}

} // namespace cpipe::json
