// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path make_temp_dir(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string{"cpipe_"} + std::string{name} + "_" + std::to_string(getpid()));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace

TEST_CASE("editor static install script copies stamped dist tree") {
    const auto root = make_temp_dir("editor_install");
    const auto dist = root / "dist";
    const auto prefix = root / "prefix";
    std::filesystem::create_directories(dist);
    std::ofstream{dist / "index.html"} << "<!doctype html><title>cpipe</title>";
    std::ofstream{dist / "app.js"} << "console.log('cpipe');";
    std::ofstream{dist / ".stamp"} << "ok\n";

    const std::string command = std::string{CMAKE_COMMAND} + " -DCPIPE_WEB_DIST_DIR=\"" +
                                dist.string() + "\" -DCMAKE_INSTALL_PREFIX=\"" + prefix.string() +
                                "\" -P \"" + CPIPE_INSTALL_EDITOR_STATIC_SCRIPT + "\"";
    REQUIRE(std::system(command.c_str()) == 0);

    const auto installed = prefix / "share" / "cpipe" / "editor";
    REQUIRE(std::filesystem::is_regular_file(installed / "index.html"));
    REQUIRE(std::filesystem::is_regular_file(installed / "app.js"));
    REQUIRE_FALSE(std::filesystem::exists(installed / ".stamp"));
}
