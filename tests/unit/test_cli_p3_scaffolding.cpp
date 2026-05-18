// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <sys/wait.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

namespace {

int run_command(const std::string& args) {
    const auto command = std::string{"\""} + CPIPE_CLI_PATH + "\" " + args;
    const int status = std::system(command.c_str());
    if (status == -1) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

TEST_CASE("Phase 3 CLI stubs expose help and return not implemented") {
    REQUIRE(run_command("serve --help >/dev/null") == 0);
    for (const auto* command : {"info", "iqa", "bench"}) {
        REQUIRE(run_command(std::string{command} + " --help >/dev/null") == 0);
        REQUIRE(run_command(std::string{command} + " >/dev/null 2>/dev/null") == 100);
    }
}
